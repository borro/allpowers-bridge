#pragma once
#include "UpsState.h"
// FILE_DEVICE_UNKNOWN, buffered IO, FILE_WRITE_DATA/FILE_READ_DATA. Shared
// constants intentionally require neither Windows nor kernel headers.
constexpr unsigned long UpsSetState = (0x22u << 16) | (2u << 14) | (0x801u << 2);
constexpr unsigned long UpsGetState = (0x22u << 16) | (1u << 14) | (0x802u << 2);
constexpr wchar_t UpsDevicePath[] = L"\\\\.\\AllpowersUps";
constexpr wchar_t UpsHardwareId[] = L"ROOT\\ALLPOWERSUPS";
