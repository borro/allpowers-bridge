#pragma once
#include "OutputController.h"
#include <assert.h>
namespace OutputTests {
struct Source:HistoryFormat::ExportSource {
  uint32_t count=3,frozen=0,opens=0,closes=0;int busy=0;bool opened=false;
  HistoryFormat::ReadState open(HistoryFormat::ExportMetadata &metadata)override{
    if(busy-->0)return HistoryFormat::ReadState::Busy;
    frozen=count;opened=true;++opens;metadata={7,11,frozen,frozen?1u:0u,frozen};return HistoryFormat::ReadState::Record;
  }
  HistoryFormat::ReadState read(uint32_t ordinal,HistoryFormat::Record &record)override{
    if(ordinal==frozen)return HistoryFormat::ReadState::End;
    if(!opened||ordinal>frozen)return HistoryFormat::ReadState::Error;
    record=HistoryFormat::example();record.magic=ordinal%2?HistoryFormat::header(0x27f,3):HistoryFormat::kMagic;
    record.sequence=ordinal+1;record.session=7;record.uptimeSeconds=ordinal*60;
    record.crc=HistoryFormat::crc32(&record,28);return HistoryFormat::ReadState::Record;
  }
  void close()override{if(opened){++closes;opened=false;}}
};
struct Status:HistoryFormat::StatusProvider {
  Source &source;uint32_t captures=0,busyFrom=9000,busyTo=11000;
  explicit Status(Source &s):source(s){}
  bool capture(HistoryFormat::StatusSnapshot &s,uint32_t now)override{
    ++captures;s={};s.nowMs=now;s.state.connected=true;
    auto &t=s.state.telemetry;t.mainSeen=t.settingsSeen=true;t.mainAt=t.settingsAt=now;
    t.source=PowerSource::Mains;t.known=0x27f;t.soc=50;
    strcpy(s.state.identity.name,"AP R2500 V2.0");strcpy(s.state.identity.address,"02:00:00:00:00:03");
    s.log.ready=true;s.log.session=7;s.log.count=s.log.latest=source.count;s.log.oldest=source.count?1:0;
    s.log.liveLatest=source.count*60;s.ups.runtime_seconds=1800;
    if(now>=busyFrom&&now<busyTo){s.historyAvailable=false;s.log={};}
    return true;
  }
};
struct Sink:HistoryFormat::ExportSink {
  char line[4096]{};size_t used=0,chunk=4096;bool blocked=false;
  uint32_t now=0,statuses=0,records=0,invalid=0,lastStatus=0,maxGap=0,begins[4]{},writes=0,busyNotices=0,falseInventories=0,zeroAnchors=0;
  size_t write(const char *bytes,size_t length)override{
    ++writes;if(blocked)return 0;const auto n=length<chunk?length:chunk;
    for(size_t i=0;i<n;++i){
      if(bytes[i]!='\n'){if(used+1<sizeof(line))line[used++]=bytes[i];else ++invalid;continue;}
      line[used]=0;
      if(used){
        if(line[0]!='{'||line[used-1]!='}')++invalid;
        else {
          if(strstr(line,"\"adapter\":\"UPSDEV\"")){if(statuses){auto gap=now-lastStatus;if(gap>maxGap)maxGap=gap;}lastStatus=now;++statuses;}
          if(strstr(line,"\"history_record\":true"))++records;
          if(strstr(line,"\"history_status_busy\":true"))++busyNotices;
          if(strstr(line,"\"history_ready\":false"))++falseInventories;
          if(strstr(line,"\"graph_live_latest_s\":0"))++zeroAnchors;
          if(strstr(line,"\"history_export_begin\":true"))for(unsigned id=1;id<4;++id){char key[24];snprintf(key,sizeof(key),"\"request\":%u,",id);if(strstr(line,key))++begins[id];}
        }
      }
      used=0;
    }
    return n;
  }
};
inline void step(HistoryFormat::OutputController &output,Sink &sink,uint32_t now){sink.now=now;output.pump(now);}
inline void selfTest(){
  for(bool shortWrites:{false,true}){
    Source source;source.count=2880;Status status(source);Sink sink;if(shortWrites)sink.chunk=256;
    HistoryFormat::OutputController output(source,sink,status);
    for(uint32_t now=0;now<100;++now)step(output,sink,now*5);
    assert(output.command("HISTORY-READ 1 2880",500));
    uint32_t end=0;
    for(uint32_t now=500;output.active()&&now<60000;now+=5){sink.blocked=shortWrites&&now>=5000&&now<6000;step(output,sink,now);end=now;}
    assert(!output.active()&&!output.failed()&&output.acknowledged()==2880);
    assert(sink.records==2880&&sink.statuses>20&&!sink.invalid&&sink.maxGap<6000&&end>6000);
    assert(sink.busyNotices>0&&!sink.falseInventories&&!sink.zeroAnchors);
    assert(source.opens==1&&source.closes==1);
  }
  {
    Source source;source.busy=100;Status status(source);Sink sink;
    HistoryFormat::OutputController output(source,sink,status);
    assert(output.command("HISTORY-EXPORT 1",0));
    for(uint32_t now=0;output.active()&&now<10000;now+=5)step(output,sink,now);
    assert(!output.active()&&sink.begins[1]==1&&sink.records==3&&sink.statuses>=1&&!sink.invalid);
  }
  {
    Source source;Status status(source);Sink sink;sink.chunk=7;
    HistoryFormat::OutputController output(source,sink,status);
    step(output,sink,0);assert(sink.used>0);
    assert(output.command("HISTORY-EXPORT 1",1));assert(output.command("HISTORY-EXPORT 2",2));assert(output.command("HISTORY-EXPORT 3",3));
    const char optional[]="{\"diagnostic\":true}\n{\"devices_done\":true}\n";
    assert(output.queueOptional(optional,sizeof(optional)-1));assert(!output.queueOptional(optional,sizeof(optional)-1));
    for(uint32_t now=5;output.active()&&now<10000;now+=5){if(now<500&&now%25==0)output.command("STATUS",now);step(output,sink,now);}
    assert(!output.active()&&!output.failed()&&!sink.invalid&&sink.begins[1]==0&&sink.begins[2]==0&&sink.begins[3]==1);
    assert(status.captures<7); // Finite repeated requests coalesce, not one restart per line.
    assert(source.opens==1&&source.closes==1&&output.acknowledged()==3);
  }
  {
    Source source;Status status(source);Sink sink;
    HistoryFormat::OutputController output(source,sink,status);
    for(uint32_t now=0;now<100;now+=5)step(output,sink,now);
    output.command("HISTORY-EXPORT 1",100);uint32_t now=100;
    for(;now<2000&&!sink.begins[1];now+=5)step(output,sink,now);
    sink.chunk=7;
    for(;now<3000;++now){step(output,sink,now);sink.line[sink.used]=0;if(strstr(sink.line,"\"history_record\":true"))break;}
    assert(sink.used&&output.acknowledged()==0);
    output.command("HISTORY-EXPORT 2",now);output.command("HISTORY-EXPORT 3",now);
    sink.chunk=4096;
    for(;output.active()&&now<10000;now+=5)step(output,sink,now);
    assert(!output.active()&&sink.invalid==1&&sink.begins[2]==0&&sink.begins[3]==1&&output.acknowledged()==3);
    assert(source.opens==2&&source.closes==2);
  }
  {
    Source source;Status status(source);Sink sink;
    HistoryFormat::OutputController output(source,sink,status);
    for(uint32_t now=0;now<100;now+=5)step(output,sink,now);
    output.command("HISTORY-EXPORT 1",100);uint32_t now=100;
    for(;now<2000&&!sink.begins[1];now+=5)step(output,sink,now);
    sink.chunk=7;
    for(;now<3000;++now){step(output,sink,now);sink.line[sink.used]=0;if(strstr(sink.line,"\"history_record\":true"))break;}
    assert(sink.used&&!output.acknowledged());sink.blocked=true;
    step(output,sink,63000);assert(!output.active()&&output.busy());
    step(output,sink,67005);assert(output.paused());
    sink.blocked=false;sink.chunk=4096;output.command("STATUS",68000);
    for(uint32_t tick=68000;tick<68200;tick+=5)step(output,sink,tick);
    assert(sink.invalid==1&&sink.statuses>=2&&!output.active()&&!output.acknowledged());
    assert(source.opens==1&&source.closes==1); // STATUS-only recovery does not reopen the archive.
  }
  {
    Source source;Status status(source);Sink sink;sink.chunk=7;
    HistoryFormat::OutputController output(source,sink,status);step(output,sink,0);sink.blocked=true;
    output.command("HISTORY-EXPORT 1",1);
    for(uint32_t now=5;now<10000;now+=5){if(now%1000==0)output.command("STATUS",now);step(output,sink,now);}
    assert(output.failed()&&!output.active()&&!output.acknowledged()&&source.opens==0&&output.busy());
    assert(output.paused());const auto stoppedWrites=sink.writes;
    for(uint32_t now=10000;now<11000;now+=5)step(output,sink,now);
    assert(sink.writes==stoppedWrites); // No automatic infinite blocked-channel retries.
    sink.blocked=false;sink.chunk=4096;
    output.command("HISTORY-EXPORT 2",11100);
    for(uint32_t now=11100;output.active()&&now<13000;now+=5)step(output,sink,now);
    assert(!output.active()&&!output.failed()&&sink.begins[2]==1&&output.acknowledged()==3);
    assert(!output.command("HISTORY-EXPORT 4294967296",13000));
    assert(!output.command("HISTORY-READ 1 2881",13000));
    assert(!output.queueOptional("invalid",7));
  }
}
}
