#include "../../apps/windows/ups_state.h"
#include "UpsState.h"
#include <cassert>
#include <initializer_list>
int main() {
  using namespace AllpowersUps;
  ap_snapshot input{};
  input.fresh = input.main_seen = input.settings_seen = 1;
  input.model = 3;
  input.known = AllpowersModels::Soc | AllpowersModels::MainsSource;
  input.soc = 50;
  input.source = 2;
  auto s = makeUpsState(input, 1, 1);
  assert(valid(s) && (s.flags & Present) && (s.flags & Mains));
  assert(!(s.flags & RuntimeKnown)); // No invented time or watt/% rate.
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
