#pragma once
#include <Arduino.h>
#include <atomic>
#include <new>
#include <Preferences.h>
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "Telemetry.h"
#include "HistoryRing.h"
#include "HistoryWire.h"
#include "HistorySample.h"
#include "HistoryClock.h"
#include "BootEpoch.h"
#include "HistoryExport.h"
#include "GraphHistory.h"

#include "HistoryStatus.h"
class HistoryPartition:public HistoryFormat::Flash {
public:
  const esp_partition_t *partition=nullptr;
  bool read(uint32_t offset,void *data,size_t n)override{
    return partition&&offset+n<=HistoryFormat::kBytes&&esp_partition_read(partition,offset,data,n)==ESP_OK;
  }
  bool write(uint32_t offset,const void *data,size_t n)override{
    return partition&&offset+n<=HistoryFormat::kBytes&&esp_partition_write(partition,offset,data,n)==ESP_OK;
  }
  bool erase(uint32_t offset)override{
    return partition&&offset%HistoryFormat::kSector==0&&offset+HistoryFormat::kSector<=HistoryFormat::kBytes&&
      esp_partition_erase_range(partition,offset,HistoryFormat::kSector)==ESP_OK;
  }
};
class HistoryStore {
  HistoryPartition flash;HistoryFormat::Ring ring{flash};
  HistoryFormat::Record *records=nullptr;
  HistoryFormat::Record *liveMemory=nullptr;
  GraphHistory::LiveRing *live=nullptr;
  SemaphoreHandle_t operation=nullptr,dataMutex=nullptr;
  std::atomic<bool> stopRequested{false};
  HistoryStats stats;uint32_t head=0;
  HistoryFormat::ClockAnchor clock;
  void error(const char *message){
    if(dataMutex)xSemaphoreTake(dataMutex,portMAX_DELAY);
    stats.error=message;stats.errors++;stats.ready=false;
    if(dataMutex)xSemaphoreGive(dataMutex);
  }
  void push(const HistoryFormat::Record &r){
    xSemaphoreTake(dataMutex,portMAX_DELAY);
    if(stats.count<HistoryFormat::kKeep)records[(head+stats.count++)%HistoryFormat::kKeep]=r;
    else {records[head]=r;head=(head+1)%HistoryFormat::kKeep;}
    stats.latest=r.sequence;stats.oldest=records[head].sequence;++stats.generation;xSemaphoreGive(dataMutex);
  }
  HistoryFormat::Record makeRecord(const Telemetry &s,bool gap){
    uint64_t uptime=uint64_t(esp_timer_get_time())/1000000;
    HistoryFormat::Record r=HistoryFormat::sample(s,millis());r.uptimeSeconds=uint32_t(uptime);
    xSemaphoreTake(dataMutex,portMAX_DELAY);r.session=stats.session;
    if((r.epochSeconds=clock.at(uptime)))r.flags|=HistoryFormat::Clock;
    xSemaphoreGive(dataMutex);
    if(gap)r.flags|=HistoryFormat::Gap;
    return r;
  }
public:
  bool begin(){
    operation=xSemaphoreCreateMutex();dataMutex=xSemaphoreCreateMutex();
    return operation&&dataMutex;
  }
  void initialize(){
    if(!operation||!dataMutex)return;
    xSemaphoreTake(operation,portMAX_DELAY);
    uint8_t *test=static_cast<uint8_t*>(ps_malloc(HistoryFormat::kBytes));
    bool tested=test&&HistoryFormat::selfTest(test);
    bool graphTest=test&&GraphHistory::selfTest(reinterpret_cast<HistoryFormat::Record*>(test));free(test);
    struct BootStorage:HistoryFormat::EpochStorage {
      Preferences prefs;
      bool read(uint32_t &value)override{
        value=prefs.isKey("boots")?prefs.getUInt("boots",UINT32_MAX):0;
        return value!=UINT32_MAX;
      }
      bool write(uint32_t value)override{return prefs.putUInt("boots",value)==sizeof(value);}
    } bootStorage;
    if(!bootStorage.prefs.begin("ap-history",false)){error("boot_counter");xSemaphoreGive(operation);return;}
    uint32_t boot=0;const bool saved=HistoryFormat::reserveEpoch(bootStorage,boot);
    bootStorage.prefs.end();
    if(!saved){error("boot_counter");xSemaphoreGive(operation);return;}
    xSemaphoreTake(dataMutex,portMAX_DELAY);stats.selfTest=tested;stats.session=boot;xSemaphoreGive(dataMutex);
    xSemaphoreTake(dataMutex,portMAX_DELAY);stats.graphTest=graphTest;xSemaphoreGive(dataMutex);
    if(!tested||!graphTest){error("self_test");xSemaphoreGive(operation);return;}
    if(stopRequested.load()){xSemaphoreGive(operation);return;}
    auto *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,esp_partition_subtype_t(0x40),"history");
    if(!part||part->address!=0xfd0000||part->size!=HistoryFormat::kBytes||part->encrypted){
      error("partition");xSemaphoreGive(operation);return;
    }
    flash.partition=part;
    auto *archive=static_cast<HistoryFormat::Record*>(ps_malloc(HistoryFormat::kKeep*sizeof(HistoryFormat::Record)));
    auto *recent=static_cast<HistoryFormat::Record*>(ps_malloc(GraphHistory::kLiveKeep*sizeof(HistoryFormat::Record)));
    if(!archive||!recent){free(archive);free(recent);error("memory");xSemaphoreGive(operation);return;}
    auto *liveRing=new(std::nothrow) GraphHistory::LiveRing(recent);
    if(!liveRing){free(archive);free(recent);error("memory");xSemaphoreGive(operation);return;}
    xSemaphoreTake(dataMutex,portMAX_DELAY);records=archive;liveMemory=recent;live=liveRing;xSemaphoreGive(dataMutex);
    if(!ring.recover()){error("read");xSemaphoreGive(operation);return;}
    uint32_t latest=ring.latestSequence();
    if(latest){
      for(uint32_t back=HistoryFormat::kSlots;back>0;--back){
        // Sequence zero is reserved and skipped at wrap.
        uint32_t sequence=latest>=back?latest-(back-1):UINT32_MAX-(back-latest-1);HistoryFormat::Record r;
        if(sequence&&ring.readSequence(sequence,r))push(r);
        if(!ring.ok()){error("read");xSemaphoreGive(operation);return;}
      }
    }
    xSemaphoreTake(dataMutex,portMAX_DELAY);stats.recovered=stats.count;stats.latest=latest;stats.ready=true;stats.error="none";xSemaphoreGive(dataMutex);
    xSemaphoreGive(operation);
  }
  bool tryStatus(HistoryStats &copy){
    if(!dataMutex||xSemaphoreTake(dataMutex,pdMS_TO_TICKS(20))!=pdTRUE)return false;
    copy=stats;xSemaphoreGive(dataMutex);return true;
  }
  HistoryStats status(){
    HistoryStats copy;tryStatus(copy);return copy;
  }
  bool record(const Telemetry &s,bool gap){
    if(!operation||stopRequested.load()||xSemaphoreTake(operation,pdMS_TO_TICKS(10))!=pdTRUE)return false;
    if(stopRequested.load()||!status().ready){xSemaphoreGive(operation);return false;}
    HistoryFormat::Record r=makeRecord(s,gap);
    uint32_t start=millis();bool ok=ring.append(r);uint32_t elapsed=millis()-start;
    if(ok)push(r);
    xSemaphoreTake(dataMutex,portMAX_DELAY);
    stats.lastWriteMs=elapsed;if(elapsed>stats.maxWriteMs)stats.maxWriteMs=elapsed;
    if(ok)stats.writes++;else {stats.errors++;stats.error="write";stats.ready=false;}
    xSemaphoreGive(dataMutex);xSemaphoreGive(operation);return ok;
  }
  void recordLive(const Telemetry &s,bool gap){
    if(stopRequested.load()||!status().ready)return;
    auto r=makeRecord(s,gap);xSemaphoreTake(dataMutex,portMAX_DELAY);
    if(live){live->push(r);stats.liveCount=live->size();stats.liveSamples++;stats.liveLatest=r.uptimeSeconds;}
    xSemaphoreGive(dataMutex);
  }
  size_t copyGraph(GraphHistory::Point *dest,size_t capacity,bool &unknown){
    unknown=false;
    if(capacity<GraphHistory::kCapacity||!dataMutex||xSemaphoreTake(dataMutex,pdMS_TO_TICKS(20))!=pdTRUE)return 0;
    if(!records){xSemaphoreGive(dataMutex);return 0;}
    size_t n=0;uint32_t liveCount=live?live->size():0,session=stats.session;
    for(uint32_t i=0;i<stats.count;++i){const auto &r=records[(head+i)%HistoryFormat::kKeep];
      if(liveCount&&r.session==live->at(0).session&&r.uptimeSeconds>=live->at(0).uptimeSeconds)continue;
      dest[n++]={r,0,false,false};
    }
    for(uint32_t i=0;i<liveCount;++i)dest[n++]={live->at(i),0,false,false};
    xSemaphoreGive(dataMutex);
    unknown=GraphHistory::assignAges(dest,n,uint32_t(uint64_t(esp_timer_get_time())/1000000),session);
    return n;
  }
  size_t copyRecent(HistoryFormat::Record *dest,size_t capacity){
    if(!dataMutex||xSemaphoreTake(dataMutex,pdMS_TO_TICKS(20))!=pdTRUE)return 0;
    if(!records){xSemaphoreGive(dataMutex);return 0;}
    size_t n=min(capacity,size_t(stats.count));uint32_t first=(head+stats.count-n)%HistoryFormat::kKeep;
    size_t firstCount=min(n,size_t(HistoryFormat::kKeep-first));
    memcpy(dest,records+first,firstCount*sizeof(*dest));
    if(n>firstCount)memcpy(dest+firstCount,records,(n-firstCount)*sizeof(*dest));
    xSemaphoreGive(dataMutex);return n;
  }
  bool readSequence(uint32_t sequence,HistoryFormat::Record &result){
    if(!dataMutex||xSemaphoreTake(dataMutex,pdMS_TO_TICKS(20))!=pdTRUE)return false;
    if(!records){xSemaphoreGive(dataMutex);return false;}
    bool found=false;
    for(uint32_t i=0;i<stats.count;++i){auto &r=records[(head+i)%HistoryFormat::kKeep];if(r.sequence==sequence){result=r;found=true;break;}}
    xSemaphoreGive(dataMutex);return found;
  }
  bool readAtOrAfter(uint32_t sequence,HistoryFormat::Record &result){
    if(!dataMutex||xSemaphoreTake(dataMutex,pdMS_TO_TICKS(20))!=pdTRUE)return false;
    if(!records){xSemaphoreGive(dataMutex);return false;}
    bool found=HistoryFormat::atOrAfter(records,head,stats.count,HistoryFormat::kKeep,sequence,result);
    xSemaphoreGive(dataMutex);return found;
  }
  HistoryFormat::ReadState capture(HistoryFormat::FrozenArchive &archive,HistoryFormat::ExportMetadata &metadata){
    using HistoryFormat::ReadState;
    if(!dataMutex)return ReadState::Error;
    if(xSemaphoreTake(dataMutex,pdMS_TO_TICKS(20))!=pdTRUE)return ReadState::Busy;
    auto result=!stats.ready||!records?ReadState::Error:
      archive.freeze(records,head,stats.count,HistoryFormat::kKeep,stats.session,stats.generation,metadata);
    xSemaphoreGive(dataMutex);return result;
  }
  bool setTime(uint32_t epoch,uint32_t observedUptime,const Telemetry &telemetry){
    if(!operation||stopRequested.load()||xSemaphoreTake(operation,pdMS_TO_TICKS(10))!=pdTRUE)return false;
    if(!status().ready||stopRequested.load()){xSemaphoreGive(operation);return false;}
    // Retries and concurrent clients cannot move an established clock or add
    // repeated flash writes. A real dated sample durably anchors this boot.
    if(clock.synced()){xSemaphoreGive(operation);return true;}
    uint32_t base=0;
    if(!clock.candidate(epoch,observedUptime,uint64_t(esp_timer_get_time())/1000000,base)){
      xSemaphoreGive(operation);return false;
    }
    auto r=makeRecord(telemetry,false);
    if(uint64_t(base)+r.uptimeSeconds>UINT32_MAX){xSemaphoreGive(operation);return false;}
    r.epochSeconds=base+r.uptimeSeconds;r.flags|=HistoryFormat::Clock;
    uint32_t start=millis();bool ok=ring.append(r);uint32_t elapsed=millis()-start;
    if(ok)push(r);
    xSemaphoreTake(dataMutex,portMAX_DELAY);
    if(ok){clock.commit(base);stats.clockSynced=true;stats.writes++;}
    else {stats.errors++;stats.error="write";stats.ready=false;}
    stats.lastWriteMs=elapsed;if(elapsed>stats.maxWriteMs)stats.maxWriteMs=elapsed;
    xSemaphoreGive(dataMutex);xSemaphoreGive(operation);return ok;
  }
  void requestPause(){stopRequested.store(true);}
  bool pausedAndIdle(){
    if(!operation)return true;
    if(xSemaphoreTake(operation,0)!=pdTRUE)return false;xSemaphoreGive(operation);return true;
  }
};
