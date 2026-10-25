#pragma once
#include "HistoryRing.h"

// The standard maintenance path requires this compiled capability together with
// matching package metadata and application SHA256, for BOTH upgrade and rollback.
// A raw application backup alone cannot establish archive compatibility.
#define AP_HISTORY_CAPABILITY "ALLPOWERS_HISTORY_R12_W2_S32"
namespace HistoryFormat {
inline constexpr char kStorageCapability[] = AP_HISTORY_CAPABILITY;
static_assert(sizeof(Record)==32 && kMagic==0x31504841 && kMagicV2==0xa2000000);
}
