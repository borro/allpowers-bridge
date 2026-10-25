#pragma once
#include "history_operation_id.h"

// Explicit injection keeps historical numeric-only firmware fixtures repeatable.
inline HistoryOperationIds::Entropy deterministicHistoryEntropy() {
  return [sequence=quint32(0)]() mutable {
    QByteArray raw(16,0);
    qToLittleEndian<quint32>(++sequence,reinterpret_cast<uchar *>(raw.data()));
    return raw;
  };
}
