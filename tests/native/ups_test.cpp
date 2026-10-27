#include "../../apps/windows/ups_state.h"
#include "UpsState.h"
#include <cassert>
#include <initializer_list>
#include <algorithm>
#include <iterator>
int main() {
  using namespace AllpowersUps;
  // Captured packets through the production C ABI, rather than hand-setting
  // the very known bits whose propagation this integration test verifies.
  const uint8_t mainPacket[]={0xa5,0x65,0xb1,0,1,8,1,2,100,0,0,0,230,0x96,0,0x6f};
  const uint8_t mains[]={0xa5,0x65,0xb1,0,1,6,3,0,1,0,0,3,2,0x75};
  const uint8_t battery[]={0xa5,0x65,0xb1,0,1,6,3,0,1,0xff,0xff,3,2,0x75};
  auto *session=ap_create(3);
  assert(session);
  ap_snapshot decoded{};
  auto stateAt=[&](uint32_t now){
    assert(ap_get_snapshot(session,now,&decoded,sizeof(decoded)));
    return makeUpsState(decoded,1,now+1);
  };
  assert(!stateAt(0).flags);
  assert(ap_receive(session,mainPacket,sizeof(mainPacket),100));
  assert(!stateAt(100).flags); // Missing Settings.
  assert(ap_receive(session,mains,sizeof(mains),100));
  assert((stateAt(101).flags&(Present|Mains))==(Present|Mains));
  assert(decoded.known&AllpowersModels::MainsSource);
  assert(ap_receive(session,battery,sizeof(battery),200));
  assert((stateAt(201).flags&(Present|Discharging))==(Present|Discharging));
  uint8_t unknown[sizeof(mains)];
  std::copy(std::begin(mains),std::end(mains),unknown);
  unknown[9]=1;unknown[13]^=1;
  assert(ap_receive(session,unknown,sizeof(unknown),300));
  assert(!stateAt(301).flags && !(decoded.known&AllpowersModels::MainsSource));
  assert(ap_receive(session,mains,sizeof(mains),400));
  assert(!stateAt(15101).flags); // Main has expired while Settings is recent.
  assert(ap_receive(session,mainPacket,sizeof(mainPacket),20000));
  assert(!stateAt(20000).flags); // Settings alone has expired.
  ap_reset(session,3);
  assert(!stateAt(20000).flags && !decoded.known);
  assert(ap_receive(session,mains,sizeof(mains),20001));
  assert(!stateAt(20001).flags); // Missing Main after reset.
  ap_destroy(session);
  ap_snapshot input{};
  input.fresh = input.main_seen = input.settings_seen = 1;
  input.model = 3;
  input.known = AllpowersModels::Soc | AllpowersModels::MainsSource;
  input.soc = 50;
  input.source = 2;
  auto s = makeUpsState(input, 1, 1);
  assert(valid(s) && (s.flags & Present) && (s.flags & Mains));
  assert(!(s.flags & RuntimeKnown)); // No invented time or watt/% rate.
  auto charging = input;
  charging.known |= AllpowersModels::InputWatts;
  charging.input_w = 120;
  assert(makeUpsState(charging, 1, 1).flags & Charging);
  charging.soc = 100;
  assert(!(makeUpsState(charging, 1, 1).flags & Charging));
  charging.soc = 50;
  charging.known &= ~AllpowersModels::InputWatts;
  assert(!(makeUpsState(charging, 1, 1).flags & Charging));
  Cache cache;
  assert(cache.accept(s, 100));
  auto tag = cache.tag;
  assert(cache.present(15099));
  assert(!cache.accept(s, 200)); // Replay cannot extend validity.
  assert(cache.expire(15100) && !cache.tag);
  s.sequence++;
  assert(cache.accept(s, 15101) && cache.tag != tag);
  tag = cache.tag;
  s.generation++;
  s.sequence = 1;
  assert(cache.accept(s, 15102) && cache.tag != tag);
  s.generation--;
  s.sequence = 999;
  assert(!cache.accept(s, 15103));
  cache.release();
  // Battery tags never use zero, even after the counter wraps. An explicit
  // withdrawal removes presence immediately rather than waiting for expiry.
  cache.nextTag = UINT32_MAX;
  s = makeUpsState(input, 2, 1);
  assert(cache.accept(s, 0) && cache.tag == 1);
  State withdrawn;
  withdrawn.generation = 2;
  withdrawn.sequence = 2;
  assert(cache.accept(withdrawn, 1) && !cache.present(1) && !cache.tag);
  cache.release();
  input.main_age_ms = 14000;
  s = makeUpsState(input, 1, 1);
  assert(s.validForMs == 1000);
  assert(cache.accept(s, 100) && cache.expire(1100));
  input.settings_age_ms = 15000;
  assert(makeUpsState(input, 1, 2).flags == 0);
  input.settings_age_ms = input.main_age_ms = 0;
  input.source = 0;
  assert(makeUpsState(input, 1, 3).flags == 0);
  input.source = 2;
  input.settings_seen = 0;
  input.model = 1;
  assert(makeUpsState(input, 1, 3).flags & Present); // Capability-based, no model allow-list.
  input.model = 3;
  assert(
      !(makeUpsState(input, 1, 3).flags & Present)); // This protocol obtains source from Settings.
  input.settings_seen = 1;
  input.source = 1;
  input.ac_output = 0; // AC off deliberately does not hide UPS.
  input.known |= AllpowersModels::RuntimeMinutes | AllpowersModels::OutputWatts;
  input.runtime_s = 2246400;
  input.output_w = 140;
  s = makeUpsState(input, 1, 4);
  assert((s.flags & Present) && (s.flags & RuntimeKnown) && (s.flags & Discharging));
  assert(s.runtimeSeconds == 2246400);
  s.percent = 101;
  assert(!valid(s));
  s.percent = 5;
  s.flags |= Charging;
  assert(!valid(s));
  s = State{};
  assert(!valid(s));
  for (uint32_t percent : {0u, 5u, 50u, 100u}) {
    input.soc = percent;
    s = makeUpsState(input, 5, percent + 1);
    assert(valid(s) && s.percent == percent && !(s.flags & ~31u));
  }
}
