#pragma once
#include "AllpowersModels.h"
#include "UpsState.h"
#include "api.h"

inline AllpowersUps::State makeUpsState(const ap_snapshot &s, uint64_t generation,
                                        uint64_t sequence) {
  using namespace AllpowersModels;
  AllpowersUps::State result;
  result.generation = generation;
  result.sequence = sequence;
  const uint32_t required = Soc | MainsSource;
  const bool settingsSource = s.model == uint32_t(Model::R2500V2);
  const auto age =
      settingsSource && s.settings_age_ms > s.main_age_ms ? s.settings_age_ms : s.main_age_ms;
  if (!s.fresh || !s.main_seen || (settingsSource && !s.settings_seen) ||
      (s.known & required) != required || s.soc > 100 || (s.source != 1 && s.source != 2) ||
      age >= AllpowersUps::MaxAgeMs)
    return result;
  result.flags = AllpowersUps::Present;
  result.percent = s.soc;
  result.validForMs = AllpowersUps::MaxAgeMs - age;
  if (s.source == 2)
    result.flags |= AllpowersUps::Mains;
  if (s.source == 2 && (s.known & InputWatts) && s.input_w && s.soc < 100)
    result.flags |= AllpowersUps::Charging;
  if (s.source == 1 && (s.known & OutputWatts) && s.output_w)
    result.flags |= AllpowersUps::Discharging;
  if ((s.known & RuntimeMinutes) && s.runtime_s < 0x80000000u) {
    result.flags |= AllpowersUps::RuntimeKnown;
    result.runtimeSeconds = s.runtime_s;
  }
  return result;
}
