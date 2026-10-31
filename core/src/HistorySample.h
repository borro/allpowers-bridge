#pragma once
#include <TelemetryState.h>
#include "HistoryRing.h"
namespace HistoryFormat {
// TelemetryState normalizes the protocol-specific output flags once. Archive
// encoding consumes that same reduced state as the live firmware/UI paths.
inline Record sample(const Telemetry &s,uint32_t now){
  Record r{};const bool fresh=telemetryFresh(s,now);
  r.magic=header(fresh?s.known:0,s.protocol);
  r.soc=fresh?s.soc:255;r.chargeWatts=fresh?s.chargeWatts:0xffff;
  r.outputWatts=fresh?s.outputWatts:0xffff;r.runtimeMinutes=fresh?s.minutes:0xffff;
  if(fresh){
    r.flags|=Fresh;
    if(s.source==PowerSource::Mains)r.flags|=Mains;
    if(s.source==PowerSource::Battery)r.flags|=Battery;
    if(s.mainFlags&2)r.flags|=AcOutput;
    if(s.mainFlags&1)r.flags|=UsbOutput;
    if(s.settingsFlags&0x10)r.flags|=DcOutput;
  }
  return r;
}
}
