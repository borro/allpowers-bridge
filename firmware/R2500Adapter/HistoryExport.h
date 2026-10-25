#pragma once
#include "HistoryWire.h"
namespace HistoryFormat {
enum class ReadState { Record, End, Busy, Error };
struct ExportMetadata { uint32_t session=0,generation=0,count=0,oldest=0,latest=0; };
class FrozenArchive {
  Record *records;uint32_t capacity,count=0;
public:
  FrozenArchive(Record *memory,uint32_t size):records(memory),capacity(size){}
  ReadState freeze(const Record *input,uint32_t head,uint32_t size,uint32_t inputCapacity,
                   uint32_t session,uint32_t generation,ExportMetadata &metadata){
    if((!records&&size)||(!input&&size)||size>capacity||size>inputCapacity)return ReadState::Error;
    for(uint32_t i=0;i<size;++i)records[i]=input[(head+i)%inputCapacity];
    count=size;metadata={session,generation,count,count?records[0].sequence:0,count?records[count-1].sequence:0};
    return ReadState::Record;
  }
  ReadState read(uint32_t ordinal,Record &record)const{
    if(ordinal==count)return ReadState::End;
    if(ordinal>count||!records)return ReadState::Error;
    record=records[ordinal];return ReadState::Record;
  }
};
struct ExportSource {
  virtual ReadState open(ExportMetadata &metadata)=0;
  virtual ReadState read(uint32_t ordinal,Record &record)=0;
  virtual void close()=0;
  virtual ~ExportSource()=default;
};
struct ExportSink {
  virtual size_t write(const char *bytes,size_t length)=0;
  virtual ~ExportSink()=default;
};
// A frozen, bounded snapshot is owned by the source until close(). Record/End,
// lock contention, and failed storage are distinct. The acknowledgement and
// digest advance only after the complete JSON line has been accepted by sink.
class ExportSession {
  ExportSource &source;ExportSink &sink;ExportMetadata metadata{};
  enum class Line { None,Begin,Record,Done } line=Line::None;
  char bytes[768]{};size_t length=0,offset=0;
  uint32_t request=0,started=0,ordinal=0,sent=0,cursor=0,digest=kDigestSeed;
  uint32_t first=0,span=0;bool running=false,opened=false,legacy=false,expired=false,needsResync=false;
  Record pending{};
  void stop(){
    // A cancelled/deadline-abandoned prefix remains in the receiver's buffer.
    // Keep this state across cancel/start, including repeated failed retries.
    if(offset)needsResync=true;
    source.close();running=false;line=Line::None;length=offset=0;
  }
  void queue(Line kind,int size){
    if(size<=0||size_t(size)>=sizeof(bytes)){stop();return;}
    line=kind;length=size_t(size);offset=0;
  }
  void done(const char *status){
    if(legacy)queue(Line::Done,snprintf(bytes,sizeof(bytes),
      "{\"history_dump_done\":true,\"history_legacy_status\":\"%s\",\"history_sent\":%lu}\n",status,(unsigned long)sent));
    else queue(Line::Done,snprintf(bytes,sizeof(bytes),
      "{\"history_dump_done\":true,\"history_export_version\":3,\"request\":%lu,\"export_session\":%lu,\"generation\":%lu,\"oldest\":%lu,\"latest\":%lu,\"count\":%lu,\"sent\":%lu,\"cursor\":%lu,\"digest\":%lu,\"status\":\"%s\"}\n",
      (unsigned long)request,(unsigned long)metadata.session,(unsigned long)metadata.generation,
      (unsigned long)metadata.oldest,(unsigned long)metadata.latest,(unsigned long)metadata.count,
      (unsigned long)sent,(unsigned long)cursor,(unsigned long)digest,status));
  }
public:
  ExportSession(ExportSource &input,ExportSink &output):source(input),sink(output){}
  bool active()const{return running;}
  uint32_t acknowledged()const{return cursor;}
  bool atLineBoundary()const{return !offset&&!needsResync;}
  bool framingPending()const{return needsResync;}
  bool syncBoundary(){
    if(!needsResync)return true;
    const size_t n=sink.write("\n",1);
    if(n>1){stop();return false;}
    if(n==1)needsResync=false;
    return !needsResync;
  }
  void cancel(){stop();}
  void start(uint32_t id,uint32_t now,uint32_t legacyFirst=0,uint32_t legacySpan=0){
    stop();metadata={};request=id;started=now;ordinal=sent=cursor=0;digest=kDigestSeed;
    first=legacyFirst;span=legacySpan;legacy=legacyFirst!=0;opened=false;expired=false;running=true;
  }
  void pump(uint32_t now){
    if(!running)return;
    const uint32_t elapsed=uint32_t(now-started);
    if(elapsed>62000){stop();return;}
    if(elapsed>60000&&!expired){
      expired=true;
      // Never splice a completion into a partially accepted JSON line.
      if(offset==0){line=Line::None;length=0;done("error");}
    }
    if(needsResync){
      syncBoundary();
      return; // A framing barrier never advances a record acknowledgement.
    }
    if(line!=Line::None){
      const size_t remaining=length-offset,n=sink.write(bytes+offset,remaining);
      if(n>remaining){needsResync=true;stop();return;}
      offset+=n;if(offset!=length)return;
      const Line completed=line;line=Line::None;length=offset=0;
      if(completed==Line::Record){
        cursor=pending.sequence;digest=digestRecord(digest,pending);++sent;++ordinal;
      }
      if(completed==Line::Done){stop();return;}
      if(expired)done("error");
      return;
    }
    if(expired){done("error");return;}
    if(!opened){
      const auto result=source.open(metadata);
      if(result==ReadState::Busy)return;
      if(result!=ReadState::Record||metadata.count>kKeep||
         (metadata.count&&(!metadata.oldest||!metadata.latest))){done("error");return;}
      opened=true;
      if(!legacy)queue(Line::Begin,snprintf(bytes,sizeof(bytes),
        "{\"history_export_begin\":true,\"history_export_version\":3,\"request\":%lu,\"export_session\":%lu,\"generation\":%lu,\"count\":%lu,\"oldest\":%lu,\"latest\":%lu}\n",
        (unsigned long)request,(unsigned long)metadata.session,(unsigned long)metadata.generation,
        (unsigned long)metadata.count,(unsigned long)metadata.oldest,(unsigned long)metadata.latest));
      return;
    }
    const auto result=source.read(ordinal,pending);
    if(result==ReadState::Busy)return;
    if(result==ReadState::Error){done("error");return;}
    if(result==ReadState::End){done(ordinal==metadata.count?"complete":"partial");return;}
    if(ordinal>=metadata.count||!pending.sequence||(!version2(pending.magic)&&pending.magic!=kMagic)||
       pending.crc!=crc32(&pending,offsetof(Record,crc))){done("error");return;}
    if(legacy){
      if(sequenceDistance(first,pending.sequence)>=span){++ordinal;return;}
      queue(Line::Record,serializeLegacy(pending,bytes,sizeof(bytes)));return;
    }
    const int prefix=snprintf(bytes,sizeof(bytes),
      "{\"history_export_version\":3,\"request\":%lu,\"export_session\":%lu,\"generation\":%lu,\"ordinal\":%lu,",
      (unsigned long)request,(unsigned long)metadata.session,(unsigned long)metadata.generation,(unsigned long)ordinal);
    char record[512];const int size=serialize(pending,record,sizeof(record));
    if(prefix<=0||size<=1||size_t(size)>=sizeof(record)||size_t(prefix+size)>=sizeof(bytes)){done("error");return;}
    memcpy(bytes+prefix,record+1,size_t(size));queue(Line::Record,prefix+size-1);
  }
};
inline bool exportSelfTest(){
  struct Source:ExportSource {
    Record input[3]{},memory[3]{};FrozenArchive archive{memory,3};
    int busyOpen=2,busyRead=2,errorAt=-1,endAt=-1,opens=0;bool foreverBusy=false,closed=false;
    ReadState open(ExportMetadata &metadata)override{
      ++opens;
      if(foreverBusy||busyOpen-->0)return ReadState::Busy;
      return archive.freeze(input,0,3,3,7,11,metadata);
    }
    ReadState read(uint32_t ordinal,Record &record)override{
      if(busyRead-->0)return ReadState::Busy;
      if(int(ordinal)==errorAt)return ReadState::Error;
      if(int(ordinal)==endAt)return ReadState::End;
      return archive.read(ordinal,record);
    }
    void close()override{closed=true;}
  } source;
  struct Sink:ExportSink {
    char output[4096]{};size_t used=0,chunk=7;int failedWrites=3;
    size_t write(const char *bytes,size_t length)override{
      if(failedWrites-->0)return 0;
      const size_t size=length<chunk?length:chunk;
      if(used+size>=sizeof(output))return 0;
      memcpy(output+used,bytes,size);used+=size;return size;
    }
  } sink;
  for(unsigned i=0;i<3;++i){source.input[i]=example();auto &r=source.input[i];
    r.magic=header(0x3f,2);r.sequence=i==0?UINT32_MAX:i;
    r.crc=crc32(&r,offsetof(Record,crc));}
  ExportSession session(source,sink);session.start(91,100);
  // Startup lock contention and a short/failed USB write are not End.
  session.pump(101);session.pump(102);session.pump(103);session.pump(104);
  if(session.acknowledged())return false;
  for(uint32_t now=105;session.active()&&now<10000;++now){
    session.pump(now);
    if(const char *row=strstr(sink.output,"\"history_record\":true")){
      if(!strchr(row,'\n')&&session.acknowledged())return false;
      if(strchr(row,'\n')&&!session.acknowledged())return false;
    }
    // Source mutation/retention after capture cannot alter the frozen export.
    if(now==110)source.input[0]=source.input[2];
  }
  if(session.active()||session.acknowledged()!=2||!strstr(sink.output,"\"sent\":3")||
     !strstr(sink.output,"\"status\":\"complete\""))return false;
  Source early;early.busyOpen=early.busyRead=0;early.endAt=1;
  memcpy(early.input,source.memory,sizeof(early.input));Sink partial;partial.chunk=4095;
  ExportSession truncated(early,partial);truncated.start(92,0);
  for(uint32_t now=0;truncated.active()&&now<100;++now)truncated.pump(now);
  if(!strstr(partial.output,"\"status\":\"partial\""))return false;
  Source failed;failed.busyOpen=failed.busyRead=0;failed.errorAt=0;
  memcpy(failed.input,source.memory,sizeof(failed.input));Sink error;error.chunk=4095;
  ExportSession broken(failed,error);broken.start(93,0);
  for(uint32_t now=0;broken.active()&&now<100;++now)broken.pump(now);
  if(!strstr(error.output,"\"status\":\"error\""))return false;
  Source blocked;blocked.foreverBusy=true;Sink timeout;timeout.failedWrites=0;timeout.chunk=4095;
  ExportSession timed(blocked,timeout);timed.start(94,0);timed.pump(1);timed.pump(60001);timed.pump(60002);
  if(timed.active()||!strstr(timeout.output,"\"status\":\"error\""))return false;
  Source stalled;stalled.busyOpen=stalled.busyRead=0;
  memcpy(stalled.input,source.memory,sizeof(stalled.input));Sink blockedSink;blockedSink.failedWrites=0;
  ExportSession blockedWrite(stalled,blockedSink);blockedWrite.start(95,0);
  for(uint32_t now=0;now<1000;++now){
    blockedWrite.pump(now);
    if(strstr(blockedSink.output,"\"history_record\":true")){blockedSink.failedWrites=100000;break;}
  }
  blockedWrite.pump(60001);blockedWrite.pump(62001);
  if(blockedWrite.active()||blockedWrite.acknowledged()||strstr(blockedSink.output,"\"status\":\"complete\""))return false;
  const auto prefixBytes=blockedSink.used;const auto previousOpens=stalled.opens;
  // A failed one-byte boundary survives explicit cancel plus repeated restart
  // deadlines, without opening a snapshot or moving a record acknowledgement.
  blockedWrite.cancel();blockedWrite.start(96,63000);
  blockedWrite.pump(63001);blockedWrite.pump(123001);blockedWrite.pump(125001);
  if(blockedWrite.active()||blockedWrite.acknowledged()||blockedSink.used!=prefixBytes||stalled.opens!=previousOpens)return false;
  blockedWrite.cancel();blockedWrite.start(97,126000);blockedWrite.pump(126001);blockedWrite.pump(188001);
  if(blockedWrite.active()||blockedSink.used!=prefixBytes||stalled.opens!=previousOpens)return false;
  blockedSink.failedWrites=0;blockedSink.chunk=4095;blockedWrite.start(98,189000);blockedWrite.pump(189001);
  if(blockedSink.used!=prefixBytes+1||blockedSink.output[prefixBytes]!='\n'||
     blockedWrite.acknowledged()||stalled.opens!=previousOpens)return false;
  for(uint32_t now=189002;blockedWrite.active()&&now<189100;++now)blockedWrite.pump(now);
  if(blockedWrite.active()||blockedWrite.acknowledged()!=2||stalled.opens!=previousOpens+1||
     !strstr(blockedSink.output,"\"request\":98")||!strstr(blockedSink.output,"\"status\":\"complete\""))return false;
  const auto projected=legacyProjection(source.memory[0]);
  if(projected.magic!=kMagic||projected.crc!=crc32(&projected,offsetof(Record,crc))||
     (projected.flags&Fresh)||source.memory[0].magic==kMagic)return false;
  return true;
}
}
