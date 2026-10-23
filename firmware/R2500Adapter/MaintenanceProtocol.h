#pragma once
#include <stdint.h>
struct __attribute__((packed)) ProbeReport {
  char magic[6];uint8_t version,flags;uint32_t uptime,rounds,advertisements;
  int8_t rssi;uint8_t reserved[2];char mac[18],name[22];
};
static_assert(sizeof(ProbeReport)==63,"Maintenance report layout");
