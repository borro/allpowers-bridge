#pragma once
#include <QByteArray>
#include <QtEndian>
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
inline quint32 fallbackRequest(const QByteArray &raw) {
  if (raw.size() != 16)
    return 0;
  for (int word = 0; word < 4; ++word) {
    const auto value =
        qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(raw.constData()) + word * 4);
    if (value)
      return value;
  }
  return 0;
}
} // namespace HistoryOperationIds
