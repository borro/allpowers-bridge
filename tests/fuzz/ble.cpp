#include "AllpowersModels.h"
#include <cassert>
#include <tuple>

static auto fields(const AllpowersModels::Decoded &d) {
  return std::tie(d.model,d.kind,d.known,d.receivedAtMs,d.soc,d.mainFlags,d.settingsFlags,
    d.inputWatts,d.outputWatts,d.runtimeMinutes,d.sourceRaw,d.cycleCount,d.chargeTimeRaw,
    d.acOutput,d.dcOutput,d.usbOutput,d.lightOutput,d.source);
}
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size) {
  using namespace AllpowersModels;
  for (auto model : {Model::Unknown, Model::Old10, Model::Legacy16, Model::R2500V2, Model(255)}) {
    Decoded d; d.soc=77; d.known=0xabc; d.sourceRaw=123; d.receivedAtMs=456;
    const Decoded before=d;
    auto result=decode(model,bytes,size,123456,d);
    if (result!=Result::Decoded) assert(fields(d)==fields(before));
    else {
      assert(d.kind!=Kind::None && d.receivedAtMs==123456);
      assert((d.known & ~uint32_t(0xfff))==0);
      assert(!d.has(Soc) || d.soc<=100);
      assert(d.runtimeSeconds()==uint32_t(d.runtimeMinutes)*60);
    }
  }
  return 0;
}
