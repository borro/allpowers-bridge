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
inline bool decodeTelemetry(Telemetry &s, const uint8_t *p, size_t n, uint32_t now) {
  const uint8_t prefix[] = {0xa5,0x65,0xb1,0x00,0x01};
  if (n < 8 || memcmp(p, prefix, 5) || n != size_t(p[5]) + 8) { s.invalidFrames++; return false; }
  uint8_t crc = 0; for (size_t i = 0; i < n; ++i) crc ^= p[i];
  if (crc) { s.invalidFrames++; return false; }
  if (p[6] == 1 && n == 16 && p[8] <= 100) {
    s.mainSeen = true; s.mainAt = now; s.mainFlags = p[7]; s.soc = p[8];
    s.chargeWatts = (uint16_t(p[9]) << 8) | p[10];
    s.outputWatts = (uint16_t(p[11]) << 8) | p[12];
    s.minutes = (uint16_t(p[13]) << 8) | p[14];
    s.known|=AllpowersModels::Soc|AllpowersModels::InputWatts|AllpowersModels::OutputWatts|AllpowersModels::RuntimeMinutes|AllpowersModels::AcOutput|AllpowersModels::UsbOutput;
  } else if (p[6] == 3 && n == 14) {
    s.settingsSeen = true; s.settingsAt = now; s.settingsFlags = p[7];
    s.sourceRaw = (uint16_t(p[9]) << 8) | p[10];
    s.source = s.sourceRaw == 0xffff ? PowerSource::Battery :
      s.sourceRaw == 0 ? PowerSource::Mains : PowerSource::Unknown;
    if(s.source != PowerSource::Unknown)s.lastKnownSource=s.source;
    s.known|=AllpowersModels::DcOutput|AllpowersModels::SourceRaw;
  } else { s.invalidFrames++; return false; }
  s.validFrames++; return true;
}
inline bool telemetryFresh(const Telemetry &s, uint32_t now) {
  if(!s.mainSeen||uint32_t(now-s.mainAt)>15000)return false;
  return s.protocol!=uint8_t(AllpowersModels::Model::R2500V2)||
    (s.settingsSeen&&s.source!=PowerSource::Unknown&&uint32_t(now-s.settingsAt)<=15000);
}
inline bool upsTelemetryFresh(const Telemetry &s,uint32_t now){
  return s.protocol==uint8_t(AllpowersModels::Model::R2500V2)&&telemetryFresh(s,now);
}
inline bool decodeStationTelemetry(Telemetry &s,const uint8_t *p,size_t n,uint32_t now,AllpowersModels::Model &model){
  if(model==AllpowersModels::Model::R2500V2){s.protocol=uint8_t(model);return decodeTelemetry(s,p,n,now);}
  AllpowersModels::Decoded d;
  if(AllpowersModels::decode(model,p,n,now,d)!=AllpowersModels::Result::Decoded){s.invalidFrames++;return false;}
  model=d.model;s.protocol=uint8_t(model);s.source=s.lastKnownSource=PowerSource::Unknown;
  if(d.kind==AllpowersModels::Kind::Main){
    s.mainSeen=true;s.mainAt=now;s.soc=d.soc;s.chargeWatts=d.inputWatts;s.outputWatts=d.outputWatts;s.minutes=d.runtimeMinutes;
    s.mainFlags=(d.acOutput?2:0)|(d.usbOutput?1:0);s.settingsFlags=d.dcOutput?0x10:0;
    s.known=d.known;
  }else if(d.kind==AllpowersModels::Kind::Settings){s.settingsSeen=true;s.settingsAt=now;}
  s.validFrames++;return true;
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
