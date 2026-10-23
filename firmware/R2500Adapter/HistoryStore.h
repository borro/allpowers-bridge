#pragma once
#include <Arduino.h>
#include <atomic>
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "Telemetry.h"
#include "HistoryRing.h"
#include "GraphHistory.h"

struct HistoryStats {
  bool ready=false,selfTest=false,clockSynced=false;
  bool graphTest=false;
  uint32_t liveCount=0,liveSamples=0,liveLatest=0;
  uint32_t count=0,recovered=0,latest=0,writes=0,errors=0,lastWriteMs=0,maxWriteMs=0,session=0;
  const char *error="initializing";
};
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
  uint32_t epochBase=0;uint64_t epochUptime=0;
  void error(const char *message){
    if(dataMutex)xSemaphoreTake(dataMutex,portMAX_DELAY);
    stats.error=message;stats.errors++;stats.ready=false;
    if(dataMutex)xSemaphoreGive(dataMutex);
  }
  void push(const HistoryFormat::Record &r){
    xSemaphoreTake(dataMutex,portMAX_DELAY);
    if(stats.count<HistoryFormat::kKeep)records[(head+stats.count++)%HistoryFormat::kKeep]=r;
    else {records[head]=r;head=(head+1)%HistoryFormat::kKeep;}
    stats.latest=r.sequence;xSemaphoreGive(dataMutex);
  }
  HistoryFormat::Record makeRecord(const Telemetry &s,bool gap){
    uint64_t uptime=uint64_t(esp_timer_get_time())/1000000;
    HistoryFormat::Record r={};r.uptimeSeconds=uint32_t(uptime);
    xSemaphoreTake(dataMutex,portMAX_DELAY);r.session=stats.session;
    if(epochBase){r.epochSeconds=epochBase+uint32_t(uptime-epochUptime);r.flags|=HistoryFormat::Clock;}
    xSemaphoreGive(dataMutex);
    if(gap)r.flags|=HistoryFormat::Gap;
    bool fresh=telemetryFresh(s,millis());
    r.soc=fresh?s.soc:255;r.chargeWatts=fresh?s.chargeWatts:0xffff;
    r.outputWatts=fresh?s.outputWatts:0xffff;r.runtimeMinutes=fresh?s.minutes:0xffff;
    if(fresh){
      r.flags|=HistoryFormat::Fresh;
      if(s.source==PowerSource::Mains)r.flags|=HistoryFormat::Mains;
      if(s.source==PowerSource::Battery)r.flags|=HistoryFormat::Battery;
      if(s.mainFlags&2)r.flags|=HistoryFormat::AcOutput;
      if(s.mainFlags&1)r.flags|=HistoryFormat::UsbOutput;
      if(s.settingsFlags&0x10)r.flags|=HistoryFormat::DcOutput;
    }
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
    xSemaphoreTake(dataMutex,portMAX_DELAY);stats.selfTest=tested;stats.session=esp_random();if(!stats.session)stats.session=1;xSemaphoreGive(dataMutex);
    xSemaphoreTake(dataMutex,portMAX_DELAY);stats.graphTest=graphTest;xSemaphoreGive(dataMutex);
    if(!tested||!graphTest){error("self_test");xSemaphoreGive(operation);return;}
    if(stopRequested.load()){xSemaphoreGive(operation);return;}
    auto *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,esp_partition_subtype_t(0x40),"history");
    if(!part||part->address!=0xfd0000||part->size!=HistoryFormat::kBytes||part->encrypted){
      error("partition");xSemaphoreGive(operation);return;
    }
    flash.partition=part;
    records=static_cast<HistoryFormat::Record*>(ps_malloc(HistoryFormat::kKeep*sizeof(HistoryFormat::Record)));
    liveMemory=static_cast<HistoryFormat::Record*>(ps_malloc(GraphHistory::kLiveKeep*sizeof(HistoryFormat::Record)));
    if(!records||!liveMemory){error("memory");xSemaphoreGive(operation);return;}
    live=new GraphHistory::LiveRing(liveMemory);
    if(!ring.recover()){error("read");xSemaphoreGive(operation);return;}
    uint32_t latest=ring.latestSequence();
    if(latest){
      for(uint32_t back=HistoryFormat::kKeep;back>0;--back){
        uint32_t sequence=latest-(back-1);HistoryFormat::Record r;
        if(sequence&&ring.readSequence(sequence,r))push(r);
        if(!ring.ok()){error("read");xSemaphoreGive(operation);return;}
      }
    }
    xSemaphoreTake(dataMutex,portMAX_DELAY);stats.recovered=stats.count;stats.latest=latest;stats.ready=true;stats.error="none";xSemaphoreGive(dataMutex);
    xSemaphoreGive(operation);
  }
  HistoryStats status(){
    HistoryStats copy;if(!dataMutex)return copy;
    if(xSemaphoreTake(dataMutex,pdMS_TO_TICKS(20))==pdTRUE){copy=stats;xSemaphoreGive(dataMutex);}return copy;
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
    if(capacity<GraphHistory::kCapacity||!dataMutex||!records||xSemaphoreTake(dataMutex,pdMS_TO_TICKS(20))!=pdTRUE)return 0;
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
    if(!dataMutex||!records||xSemaphoreTake(dataMutex,pdMS_TO_TICKS(20))!=pdTRUE)return 0;
    size_t n=min(capacity,size_t(stats.count));uint32_t first=(head+stats.count-n)%HistoryFormat::kKeep;
    size_t firstCount=min(n,size_t(HistoryFormat::kKeep-first));
    memcpy(dest,records+first,firstCount*sizeof(*dest));
    if(n>firstCount)memcpy(dest+firstCount,records,(n-firstCount)*sizeof(*dest));
    xSemaphoreGive(dataMutex);return n;
  }
  bool readSequence(uint32_t sequence,HistoryFormat::Record &result){
    if(!dataMutex||!records||xSemaphoreTake(dataMutex,pdMS_TO_TICKS(20))!=pdTRUE)return false;
    bool found=false;
    for(uint32_t i=0;i<stats.count;++i){auto &r=records[(head+i)%HistoryFormat::kKeep];if(r.sequence==sequence){result=r;found=true;break;}}
    xSemaphoreGive(dataMutex);return found;
  }
  void setTime(uint32_t epoch){
    if(!dataMutex)return;
    xSemaphoreTake(dataMutex,portMAX_DELAY);epochBase=epoch;epochUptime=uint64_t(esp_timer_get_time())/1000000;stats.clockSynced=true;xSemaphoreGive(dataMutex);
  }
  void requestPause(){stopRequested.store(true);}
  bool pausedAndIdle(){
    if(!operation)return true;
    if(xSemaphoreTake(operation,0)!=pdTRUE)return false;xSemaphoreGive(operation);return true;
  }
};
