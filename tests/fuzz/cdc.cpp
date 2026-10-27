#include "OutputController.h"
#include <cassert>
#include <vector>
using namespace HistoryFormat;
struct Source : ExportSource {
  ReadState open(ExportMetadata &m) override { m={1,1,0,0,0}; return ReadState::Record; }
  ReadState read(uint32_t,Record &) override { return ReadState::End; }
  void close() override {}
};
struct Sink : ExportSink {
  size_t chunk;
  explicit Sink(size_t n):chunk(n){}
  size_t write(const char *p,size_t n) override {
    // Read the bytes, so an erroneous length cannot escape ASan detection.
    volatile unsigned checksum=0;
    for(size_t i=0;i<n;++i) checksum=checksum+static_cast<unsigned char>(p[i]);
    return n<chunk?n:chunk;
  }
};
struct Status : StatusProvider {
  bool capture(StatusSnapshot &s,uint32_t now) override { s={}; s.nowMs=now; return true; }
};
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *bytes,size_t size) {
  Source source; Sink sink(size?bytes[0]:0); Status status;
  OutputController output(source,sink,status);
  std::vector<char> command(bytes,bytes+size); command.push_back(0);
  output.command(command.data(),0);
  HistoryCommandLine collector;
  uint32_t now=1;
  for(size_t i=0;i<size;++i) {
    if(const char *line=collector.feed(char(bytes[i]))) {
      assert(historyCommandLength(line)<kHistoryCommandCapacity);
      output.command(line,now);
    }
    output.pump(now++);
    assert(output.acknowledged()==0); // Empty source cannot acknowledge records.
  }
  if(const char *line=collector.feed('\n')) output.command(line,now);
  output.pump(now+62001);
  assert(!output.active() && output.acknowledged()==0);
  return 0;
}
