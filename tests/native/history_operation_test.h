#pragma once
#include "output_test.h"
#include <string>
#include <vector>
namespace HistoryOperationTests {
constexpr const char *nonceA="0123456789abcdef0123456789abcdef";
constexpr const char *nonceB="fedcba9876543210fedcba9876543210";
struct Source:OutputTests::Source {
  bool failOpen=false;int failAt=-1;
  HistoryFormat::ReadState open(HistoryFormat::ExportMetadata &metadata)override{
    return failOpen?HistoryFormat::ReadState::Error:OutputTests::Source::open(metadata);
  }
  HistoryFormat::ReadState read(uint32_t ordinal,HistoryFormat::Record &record)override{
    return int(ordinal)==failAt?HistoryFormat::ReadState::Error:OutputTests::Source::read(ordinal,record);
  }
};
struct Sink:HistoryFormat::ExportSink {
  std::string bytes;size_t chunk=7;int zeroWrites=4;
  size_t write(const char *data,size_t length)override{
    if(zeroWrites-->0)return 0;
    const auto size=length<chunk?length:chunk;bytes.append(data,size);return size;
  }
};
inline std::vector<std::string> frames(const std::string &bytes){
  std::vector<std::string> result;size_t offset=0;
  while(offset<bytes.size()){
    const auto end=bytes.find('\n',offset);if(end==std::string::npos)break;
    auto line=bytes.substr(offset,end+1-offset);offset=end+1;
    if(line.find("\"history_export_begin\":true")!=std::string::npos||
       line.find("\"history_record\":true")!=std::string::npos||
       line.find("\"history_dump_done\":true")!=std::string::npos)result.push_back(line);
  }
  return result;
}
inline void echoed(const std::vector<std::string> &lines,const char *nonce,size_t expected){
  assert(lines.size()==expected);
  const auto field=std::string("\"operation_nonce\":\"")+nonce+'"';
  for(const auto &line:lines)assert(line.find(field)!=std::string::npos);
}
inline void selfTest(){
  using namespace HistoryFormat;
  assert(kHistoryNonceBits==128&&kHistoryNonceHexLength==32&&kHistoryCommandCapacity==80);
  assert(validOperationNonce(nonceA,32)&&validOperationNonce(nonceB,32));
  assert(!validOperationNonce(nonceA,31)&&!validOperationNonce(nonceA,33)&&!validOperationNonce(nullptr,32));
  assert(!validOperationNonce("0123456789abcdef0123456789abcdeG",32));
  {
    Source source;source.busy=3;OutputTests::Status status(source);Sink sink;
    OutputController output(source,sink,status);
    std::string command=std::string("HISTORY-EXPORT 1 ")+nonceA;
    assert(output.command(command.c_str(),0));
    command.assign(command.size(),'x'); // Export owns a copy, not this caller's token buffer.
    for(uint32_t now=0;output.active()&&now<15000;now+=5)output.pump(now);
    assert(!output.active()&&!output.failed()&&source.opens==1&&source.closes==1&&output.acknowledged()==3);
    echoed(frames(sink.bytes),nonceA,5);
    assert(sink.bytes.find("\"history_nonce_bits\":128")!=std::string::npos);
  }
  {
    Source source;source.count=10;OutputTests::Status status(source);Sink sink;sink.chunk=4096;sink.zeroWrites=0;
    OutputController output(source,sink,status);
    const auto valid=std::string("HISTORY-EXPORT 1 ")+nonceA;assert(output.command(valid.c_str(),0));
    for(uint32_t now=0;now<1000&&(!source.opened||sink.bytes.find("history_export_begin")==std::string::npos);now+=5)output.pump(now);
    assert(source.opened&&source.opens==1&&source.closes==0);
    const auto acknowledged=output.acknowledged();
    for(const auto &bad:std::vector<std::string>{
      std::string("HISTORY-EXPORT 2 ")+std::string(nonceA,31),
      std::string("HISTORY-EXPORT 2 ")+nonceA+'0',
      "HISTORY-EXPORT 2 0123456789abcdef0123456789abcdeG",
      "HISTORY-EXPORT 2 0123456789ABCDEF0123456789ABCDEF",
      std::string("HISTORY-EXPORT 2 ")+nonceA+" garbage",
      "HISTORY-EXPORT 2 ","HISTORY-EXPORT 0 0123456789abcdef0123456789abcdef",
      "HISTORY-EXPORT 4294967296 0123456789abcdef0123456789abcdef",
      std::string(80,'x')+"HISTORY-EXPORT 2"}){
      assert(!output.command(bad.c_str(),1000));assert(source.opened&&source.closes==0&&output.active());
      assert(output.acknowledged()==acknowledged);
    }
    HistoryCommandLine input;
    const auto overflow=std::string(kHistoryCommandCapacity,'x')+"HISTORY-EXPORT 2 "+nonceB+"\n";
    for(char byte:overflow)assert(input.feed(byte)==nullptr);
    assert(!input.discarding()&&source.opened&&source.closes==0);
    const char hiddenBytes[]="HISTORY-EXPORT 2\0 suffix\n";
    const std::string hidden(hiddenBytes,sizeof(hiddenBytes)-1);
    for(char byte:hidden)assert(input.feed(byte)==nullptr);
    const auto replacement=std::string("HISTORY-EXPORT 2 ")+nonceB+"\r\n";
    unsigned dispatched=0;
    for(char byte:replacement)if(const char *line=input.feed(byte)){assert(output.command(line,1001));++dispatched;}
    assert(dispatched==1&&source.closes==1&&output.active());
    const auto from=sink.bytes.size();
    for(uint32_t now=1005;output.active()&&now<10000;now+=5)output.pump(now);
    echoed(frames(sink.bytes.substr(from)),nonceB,12);
  }
  for(bool failOpen:{false,true}){
    Source source;source.failOpen=failOpen;source.failAt=0;Sink sink;
    ExportSession session(source,sink);session.start(7,0,0,0,nonceA);
    for(uint32_t now=0;session.active()&&now<10000;++now)session.pump(now);
    assert(!session.active()&&!session.acknowledged());
    const auto lines=frames(sink.bytes);echoed(lines,nonceA,failOpen?1:2);
    assert(lines.back().find("\"status\":\"error\"")!=std::string::npos);
    if(failOpen)assert(lines.back().find("\"export_session\":0")!=std::string::npos);
  }
  {
    struct Maximum:HistoryFormat::ExportSource {
      ReadState open(ExportMetadata &metadata)override{metadata={UINT32_MAX,UINT32_MAX,1,UINT32_MAX,UINT32_MAX};return ReadState::Record;}
      ReadState read(uint32_t ordinal,Record &record)override{
        if(ordinal)return ReadState::End;
        record={};record.magic=header(4095,3);record.sequence=record.session=record.uptimeSeconds=record.epochSeconds=UINT32_MAX;
        record.chargeWatts=record.outputWatts=record.runtimeMinutes=UINT16_MAX;record.soc=100;record.flags=251;
        record.crc=crc32(&record,28);return ReadState::Record;
      }
      void close()override{}
    } source;
    Sink sink;ExportSession session(source,sink);session.start(UINT32_MAX,0,0,0,nonceA);
    for(uint32_t now=0;session.active()&&now<10000;++now)session.pump(now);
    const auto lines=frames(sink.bytes);echoed(lines,nonceA,3);
    for(const auto &line:lines)assert(line.size()<768&&line.front()=='{'&&line[line.size()-2]=='}');
  }
  {
    Source source;Sink sink;sink.chunk=4096;sink.zeroWrites=0;
    ExportSession session(source,sink);session.start(1,0,0,0,nonceA);
    session.pump(0);assert(source.opened);
    session.start(2,1,0,0,"invalid");assert(source.opened&&source.closes==0);
    session.start(2,2);assert(source.closes==1);
    sink.bytes.clear();
    for(uint32_t now=3;session.active()&&now<100;++now)session.pump(now);
    const auto lines=frames(sink.bytes);assert(lines.size()==5);
    assert(lines.front()=="{\"history_export_begin\":true,\"history_export_version\":3,\"request\":2,\"export_session\":7,\"generation\":11,\"count\":3,\"oldest\":1,\"latest\":3}\n");
    for(const auto &line:lines)assert(line.find("operation_nonce")==std::string::npos);
  }
}
}
