#pragma once
#include "HistoryExport.h"
#include "StatusWire.h"
#include <string.h>
namespace HistoryFormat {
// Sole byte-stream owner. Commands run before pump() on the loop task; callbacks
// enqueue optional diagnostics in a separate bounded mailbox and never write USB.
class OutputController {
  ExportSession archive;ExportSink &sink;StatusProvider &provider;
  StatusSnapshot status{};
  enum class Owner {None,Status,Optional} owner=Owner::None;
  char statusLine[3072]{},optional[6144]{};
  size_t length=0,offset=0,optionalSize=0,optionalPosition=0;
  uint8_t statusRow=0;
  bool statusPending=false,statusActive=false,statusUrgent=false,periodicStarted=false;
  bool ordinaryTurn=false,yieldArchive=false,needsResync=false,boundaryTracking=false,failed_=false,paused_=false;
  uint32_t lastStatus=0,exportStarted=0,lineStarted=0,lastProgress=0,boundaryStarted=0;
  static constexpr uint32_t kStatusInterval=1000,kStallMs=4000,kLineLimitMs=8000;
  static bool decimal(const char *&p,uint32_t &out){
    if(*p<'0'||*p>'9')return false;out=0;
    do{const uint32_t digit=uint32_t(*p++-'0');if(out>(UINT32_MAX-digit)/10)return false;out=out*10+digit;}while(*p>='0'&&*p<='9');
    return true;
  }
  void abandonOrdinary(){
    if(offset)needsResync=true;
    owner=Owner::None;length=offset=0;statusActive=false;optionalSize=optionalPosition=0;
    archive.cancel();failed_=paused_=true;
  }
  void ordinary(uint32_t now){
    if(uint32_t(now-lastProgress)>kStallMs||uint32_t(now-lineStarted)>kLineLimitMs){abandonOrdinary();return;}
    const char *bytes=owner==Owner::Status?statusLine:optional+optionalPosition;
    const size_t remaining=length-offset,n=sink.write(bytes+offset,remaining);
    if(n>remaining){needsResync=true;abandonOrdinary();return;}
    offset+=n;if(n)lastProgress=now;if(offset!=length)return;
    if(owner==Owner::Status){
      if(++statusRow==kStatusPrimaryRows){
        yieldArchive=true;lastStatus=now;
        if(!statusUrgent)statusPending=false;
      }
      if(statusRow==kStatusRows)statusActive=false;
    }else {optionalPosition+=length;if(optionalPosition==optionalSize)optionalSize=optionalPosition=0;}
    owner=Owner::None;length=offset=0;ordinaryTurn=false;
  }
  bool queueStatusLine(uint32_t now){
    const int n=serializeStatus(status,statusRow,statusLine,sizeof(statusLine));
    if(n<=0||size_t(n)>=sizeof(statusLine)){statusActive=false;failed_=true;return false;}
    owner=Owner::Status;length=size_t(n);offset=0;lineStarted=lastProgress=now;ordinary(now);return true;
  }
  void barrier(uint32_t now){
    if(!boundaryTracking){boundaryTracking=true;boundaryStarted=now;}
    if(uint32_t(now-boundaryStarted)>kStallMs){archive.cancel();failed_=paused_=true;return;}
    if(needsResync){
      const auto n=sink.write("\n",1);if(n==1)needsResync=false;
      else if(n>1){archive.cancel();failed_=true;}
    }else archive.syncBoundary();
    if(!needsResync&&!archive.framingPending())boundaryTracking=false;
  }
  bool captureStatus(uint32_t now){
    if(!provider.capture(status,now))return false;
    statusPending=statusUrgent=false;statusActive=true;statusRow=0;
    periodicStarted=true;lastStatus=now;return true;
  }
public:
  OutputController(ExportSource &source,ExportSink &output,StatusProvider &input):archive(source,output),sink(output),provider(input){}
  bool active()const{return archive.active();}
  bool busy()const{return archive.active()||owner!=Owner::None||statusPending||statusActive||optionalSize||needsResync||archive.framingPending();}
  bool failed()const{return failed_;}
  bool paused()const{return paused_;}
  uint32_t acknowledged()const{return archive.acknowledged();}
  bool queueOptional(const char *bytes,size_t size){
    if(!size||size>sizeof(optional)||bytes[size-1]!='\n'||optionalSize)return false;
    memcpy(optional,bytes,size);optionalSize=size;optionalPosition=0;return true;
  }
  bool command(const char *line,uint32_t now){
    if(!strcmp(line,"STATUS")||!strcmp(line,"HISTORY")){
      if(paused_){paused_=false;boundaryTracking=false;}
      statusPending=statusUrgent=true;return true;
    }
    uint32_t first=0,count=0,request=0;const char *p=line;
    if(!strncmp(p,"HISTORY-READ ",13)){
      p+=13;if(!decimal(p,first)||*p++!=' '||!decimal(p,count)||*p||!first||!count||count>kKeep)return false;
    }else if(!strncmp(p,"HISTORY-EXPORT ",15)){
      p+=15;if(!decimal(p,request)||*p||!request)return false;
    }else return false;
    if(paused_){paused_=false;boundaryTracking=false;}
    // Latest command wins immediately: close the old frozen snapshot even if
    // STATUS owns a partial line. That line retains its owner until drained or
    // bounded abort; archive.start preserves any abandoned archive delimiter.
    archive.start(request,now,first,count);exportStarted=now;failed_=false;
    yieldArchive=false;return true;
  }
  void pump(uint32_t now){
    // Ordinary output must not hide the export's independent hard deadline.
    if(archive.active()&&uint32_t(now-exportStarted)>62000){archive.cancel();failed_=true;}
    if(paused_)return;
    if(!periodicStarted||((!statusActive||statusRow>=kStatusPrimaryRows)&&uint32_t(now-lastStatus)>=kStatusInterval))statusPending=true;
    if(owner!=Owner::None){ordinary(now);return;}
    if(needsResync||archive.framingPending()){barrier(now);return;}
    boundaryTracking=false;
    if(archive.active()&&!archive.atLineBoundary()){archive.pump(now);return;}
    // Explicit STATUS coalesces one fresh handshake after the current complete
    // primary batch. Periodic requests never restart that batch or extend an
    // output deadline; ordinary lines always retain exclusive ownership.
    if(statusPending&&(!statusActive||statusRow>=kStatusPrimaryRows)&&
       (statusUrgent||!yieldArchive||!archive.active()))captureStatus(now);
    if(statusActive&&statusRow<kStatusPrimaryRows){queueStatusLine(now);return;}
    if(archive.active()&&(yieldArchive||!ordinaryTurn||(!statusActive&&!optionalSize))){
      yieldArchive=false;archive.pump(now);ordinaryTurn=true;return;
    }
    yieldArchive=false;
    if(statusActive){queueStatusLine(now);return;}
    if(optionalSize){
      size_t end=optionalPosition;while(end<optionalSize&&optional[end]!='\n')++end;
      if(end==optionalSize){optionalSize=optionalPosition=0;failed_=true;return;}
      owner=Owner::Optional;length=end+1-optionalPosition;offset=0;lineStarted=lastProgress=now;ordinary(now);return;
    }
    if(archive.active()){archive.pump(now);ordinaryTurn=true;}
  }
};
}
