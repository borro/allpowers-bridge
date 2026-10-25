#pragma once
#include "HistoryRing.h"
#include <stdio.h>
namespace HistoryFormat {
// Used by the actual CDC writer and desktop round-trip tests.
inline int serialize(const Record &r,char *out,size_t capacity){
  return snprintf(out,capacity,"{\"history_record\":true,\"record_version\":%u,\"known\":%lu,\"protocol\":%u,\"sequence\":%lu,\"session\":%lu,\"uptime_s\":%lu,\"epoch\":%lu,\"soc\":%u,\"charge_w\":%u,\"output_w\":%u,\"runtime_minutes\":%u,\"flags\":%u,\"crc\":%lu}\n",
    version2(r.magic)?2u:1u,(unsigned long)known(r.magic),model(r.magic),
    (unsigned long)r.sequence,(unsigned long)r.session,(unsigned long)r.uptimeSeconds,
    (unsigned long)r.epochSeconds,r.soc,r.chargeWatts,r.outputWatts,r.runtimeMinutes,r.flags,(unsigned long)r.crc);
}
// Inclusive cursor over real records, rather than assuming adjacent sequences.
inline bool atOrAfter(const Record *records,uint32_t head,uint32_t count,uint32_t capacity,
                      uint32_t cursor,Record &result){
  for(uint32_t i=0;i<count;++i){const auto &r=records[(head+i)%capacity];
    if(r.sequence==cursor||newer(r.sequence,cursor)){result=r;return true;}
  }
  return false;
}
inline bool wireSelfTest(uint8_t *memory){
  MemoryFlash flash(memory);Ring ring(flash);Record r=example(),records[2];
  if(!ring.recover()||!ring.append(r))return false;
  records[0]=r;flash.partial=8;if(ring.append(r))return false;
  Ring restored(flash);if(!restored.recover())return false;
  r=example();r.magic=header(0x3f,2); // Legacy16: USB output unknown.
  if(!restored.append(r)||r.sequence!=3)return false;
  records[1]=r;Record read;
  if(!atOrAfter(records,0,2,2,1,read)||read.sequence!=1||
     !atOrAfter(records,0,2,2,next(read.sequence),read)||read.sequence!=3||
     atOrAfter(records,0,2,2,next(read.sequence),read))return false;
  if(!restored.readSequence(3,read)||known(read.magic)!=0x3f||model(read.magic)!=2)return false;
  char json[512];int length=serialize(read,json,sizeof(json));
  if(length<=0||size_t(length)>=sizeof(json)||!strstr(json,"\"record_version\":2")||!strstr(json,"\"known\":63"))return false;
  for(uint32_t mask:{0u,64u})for(bool on:{false,true}){
    r=example();r.magic=header(mask,2);if(on)r.flags|=UsbOutput;
    if(!restored.append(r)||!restored.readSequence(r.sequence,read)||known(read.magic)!=mask||bool(read.flags&UsbOutput)!=on)return false;
  }
  // Legacy records remain valid and expose no invented provenance.
  if(!restored.readSequence(1,read)||version2(read.magic)||known(read.magic))return false;
  return true;
}
}
