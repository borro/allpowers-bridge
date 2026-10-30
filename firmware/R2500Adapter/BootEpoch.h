#pragma once
#include <stdint.h>
namespace HistoryFormat {
struct EpochStorage {
  virtual bool read(uint32_t &value) = 0;
  virtual bool write(uint32_t value) = 0;
  virtual ~EpochStorage() = default;
};
inline bool reserveEpoch(EpochStorage &storage, uint32_t &epoch) {
  uint32_t prior = 0, check = 0;
  if (!storage.read(prior) || prior == UINT32_MAX || !storage.write(prior + 1) ||
      !storage.read(check) || check != prior + 1) return false;
  epoch = check;
  return true;
}
}
