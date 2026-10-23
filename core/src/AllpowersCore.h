#pragma once
#include "AllpowersModels.h"
#include "TelemetryState.h"

namespace AllpowersCore {
constexpr uint32_t kPollIntervalMs=20000,kReconnectSilenceMs=45000;
// One state per connection, no OS handles, threads, allocation or USB policy.
// The caller owns synchronization. A reconnect/selection always resets state.
class Session {
public:
  Telemetry telemetry{};
  AllpowersModels::Model model=AllpowersModels::Model::Unknown;
  explicit Session(AllpowersModels::Model selected):model(selected){telemetry.protocol=uint8_t(model);}
  void reset(AllpowersModels::Model selected){telemetry=Telemetry{};model=selected;telemetry.protocol=uint8_t(model);}
  bool receive(const uint8_t *p,size_t n,uint32_t now){
    if(!p){telemetry.invalidFrames++;return false;}
    return decodeStationTelemetry(telemetry,p,n,now,model);
  }
  bool fresh(uint32_t now)const{return telemetryFresh(telemetry,now);}
  AllpowersModels::PollView poll()const{return AllpowersModels::readOnlyPoll(model);}
};
}
