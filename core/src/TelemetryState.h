#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "AllpowersModels.h"

enum class PowerSource : uint8_t { Unknown, Battery, Mains };
struct Telemetry {
  uint32_t mainAt = 0, settingsAt = 0, validFrames = 0, invalidFrames = 0;
  bool mainSeen = false, settingsSeen = false;
  uint8_t soc = 0, mainFlags = 0, settingsFlags = 0;
  uint16_t chargeWatts = 0, outputWatts = 0, minutes = 0, sourceRaw = 0;
  PowerSource source = PowerSource::Unknown;
  PowerSource lastKnownSource = PowerSource::Unknown;
  uint8_t protocol=uint8_t(AllpowersModels::Model::R2500V2);
  uint32_t known=0;
};
// The protocol decoder emits deltas; this is the single accumulator used by
// firmware and the C ABI. An unknown Settings source explicitly clears its bit.
inline bool decodeStationTelemetry(Telemetry &s,const uint8_t *p,size_t n,uint32_t now,AllpowersModels::Model &model){
  using namespace AllpowersModels;
  Decoded d;
  if(decode(model,p,n,now,d)!=Result::Decoded){s.invalidFrames++;return false;}
  model=d.model;s.protocol=uint8_t(model);
  if(d.kind==Kind::Main){
    s.mainSeen=true;s.mainAt=now;s.soc=d.soc;s.chargeWatts=d.inputWatts;s.outputWatts=d.outputWatts;s.minutes=d.runtimeMinutes;
    s.mainFlags=(d.acOutput?2:0)|(d.usbOutput?1:0);
    if(model==Model::R2500V2) s.known|=d.known;
    else {s.settingsFlags=d.dcOutput?0x10:0;s.known=d.known;}
  }else if(d.kind==Kind::Settings){
    s.settingsSeen=true;s.settingsAt=now;
    if(model==Model::R2500V2){
      s.settingsFlags=d.settingsFlags;s.sourceRaw=d.sourceRaw;s.source=PowerSource(d.source);
      s.known=(s.known&~uint32_t(MainsSource))|d.known;
      if(s.source!=PowerSource::Unknown)s.lastKnownSource=s.source;
    }else s.known|=d.known;
  }
  if(model!=Model::R2500V2)s.source=s.lastKnownSource=PowerSource::Unknown;
  s.validFrames++;return true;
}
inline bool decodeTelemetry(Telemetry &s, const uint8_t *p, size_t n, uint32_t now) {
  auto model=AllpowersModels::Model::R2500V2;
  return decodeStationTelemetry(s,p,n,now,model);
}
inline bool telemetryFresh(const Telemetry &s, uint32_t now) {
  if(!s.mainSeen||uint32_t(now-s.mainAt)>15000)return false;
  return s.protocol!=uint8_t(AllpowersModels::Model::R2500V2)||
    (s.settingsSeen&&s.source!=PowerSource::Unknown&&uint32_t(now-s.settingsAt)<=15000);
}
inline bool upsTelemetryFresh(const Telemetry &s,uint32_t now){
  return s.protocol==uint8_t(AllpowersModels::Model::R2500V2)&&telemetryFresh(s,now);
}
inline const char *sourceName(PowerSource source) {
  return source == PowerSource::Mains ? "mains" : source == PowerSource::Battery ? "battery" : "unknown";
}
inline bool telemetrySelfTest() {
  Telemetry s;
  const uint8_t main[] = {0xa5,0x65,0xb1,0,1,8,1,2,100,0,0,0,230,0x96,0,0x6f};
  const uint8_t battery[] = {0xa5,0x65,0xb1,0,1,6,3,0,1,0xff,0xff,3,2,0x75};
  const uint8_t mains[] = {0xa5,0x65,0xb1,0,1,6,3,0,1,0,0,3,2,0x75};
  if (!decodeTelemetry(s, main, sizeof(main), 100) || s.soc != 100 || s.outputWatts != 230 || uint32_t(s.minutes)*60 != 2304000) return false;
  if (!decodeTelemetry(s,battery,sizeof(battery),100) || s.source != PowerSource::Battery || !telemetryFresh(s,101)) return false;
  if (!decodeTelemetry(s,mains,sizeof(mains),101) || s.source != PowerSource::Mains || telemetryFresh(s,15102)) return false;
  uint8_t bad[14]; memcpy(bad,mains,14); bad[9] = 1;
  if (decodeTelemetry(s,bad,14,102)) return false;
  bad[13] ^= 1;
  if (!decodeTelemetry(s,bad,14,103) || s.source != PowerSource::Unknown || telemetryFresh(s,103)) return false;
  auto model=AllpowersModels::Model::Unknown;Telemetry generic;
  if(!decodeStationTelemetry(generic,main,sizeof(main),200,model)||model!=AllpowersModels::Model::Legacy16||
    !telemetryFresh(generic,201)||upsTelemetryFresh(generic,201)||generic.source!=PowerSource::Unknown||
    (generic.known&AllpowersModels::UsbOutput)||!(generic.known&AllpowersModels::DcOutput))return false;
  const uint8_t old[]={0x0f,42,0,100,0,200,0,7,4,0x41};model=AllpowersModels::Model::Old10;
  if(!decodeStationTelemetry(generic,old,sizeof(old),300,model)||generic.soc!=42||generic.minutes!=1089||
    !telemetryFresh(generic,301)||upsTelemetryFresh(generic,301)||generic.mainFlags!=3||generic.settingsFlags!=0x10)return false;
  return true;
}
