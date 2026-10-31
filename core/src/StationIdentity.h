#pragma once

// Values observed in the matched BLE advertisement, not a guessed model label.
struct StationIdentity {
  char name[32] = {};
  char address[18] = {};
};
