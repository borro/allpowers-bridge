#pragma once
#include <stdint.h>
struct HistoryStats {
  bool ready=false,selfTest=false,clockSynced=false;
  bool graphTest=false;
  uint32_t liveCount=0,liveSamples=0,liveLatest=0;
  uint32_t count=0,recovered=0,latest=0,oldest=0,generation=0,writes=0,errors=0,lastWriteMs=0,maxWriteMs=0,session=0;
  const char *error="initializing";
};
