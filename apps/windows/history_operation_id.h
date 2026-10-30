#pragma once
#include <QByteArray>
#include <functional>
#include <qt_windows.h>

// BCrypt declarations require the Windows types above.
#include <bcrypt.h>

namespace HistoryOperationIds {
using Entropy = std::function<QByteArray()>;
// Keep all 128 random bits; UUID formatting would reserve version/variant bits.
inline QByteArray random128() {
  QByteArray raw(16, 0);
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(raw.data()), ULONG(raw.size()),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    return {};
  return raw;
}
} // namespace HistoryOperationIds
