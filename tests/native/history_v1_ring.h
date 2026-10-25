// Frozen AHP1 Ring from 79b017332a11f78be632e33885d91c2ea0d66f0a.
// Namespace isolation and equivalent portable packing are the only adaptations.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace HistoricalAHP1 {
constexpr uint32_t kMagic=0x31504841; // AHP1, little endian
constexpr uint32_t kBytes=0x20000,kSector=4096,kSlots=kBytes/32,kKeep=2880;
enum : uint8_t {Fresh=1,Mains=2,Battery=4,AcOutput=8,UsbOutput=16,DcOutput=32,Clock=64,Gap=128};
#pragma pack(push,1)
struct Record {
  uint32_t magic,sequence,session,uptimeSeconds,epochSeconds;
  uint16_t chargeWatts,outputWatts,runtimeMinutes;
  uint8_t soc,flags;
  uint32_t crc;
};
#pragma pack(pop)
static_assert(sizeof(Record)==32 && offsetof(Record,crc)==28,"History record format");
inline uint32_t crc32(const void *data,size_t length) {
  auto *p=static_cast<const uint8_t*>(data);uint32_t crc=0xffffffff;
  for(size_t i=0;i<length;++i){crc^=p[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));}
  return ~crc;
}
inline bool newer(uint32_t a,uint32_t b){return int32_t(a-b)>0;}
inline uint32_t next(uint32_t n){return ++n?n:1;}
inline uint32_t slot(uint32_t sequence){return (sequence-1)%kSlots;}
inline bool valid(const Record &r,uint32_t physicalSlot){
  return r.magic==kMagic && r.sequence && slot(r.sequence)==physicalSlot &&
    (!(r.flags&Fresh)||r.soc<=100) && r.crc==crc32(&r,offsetof(Record,crc));
}
struct Flash {
  virtual bool read(uint32_t offset,void *data,size_t length)=0;
  virtual bool write(uint32_t offset,const void *data,size_t length)=0;
  virtual bool erase(uint32_t offset)=0;
  virtual ~Flash()=default;
};
class Ring {
  Flash &flash;uint32_t allocated=0,latest=0;bool healthy=true;
public:
  explicit Ring(Flash &f):flash(f){}
  bool recover(){
    latest=allocated=0;healthy=true;
    for(uint32_t i=0;i<kSlots;++i){Record r;
      if(!flash.read(i*sizeof(r),&r,sizeof(r))){healthy=false;return false;}
      if(valid(r,i)&&(!latest||newer(r.sequence,latest)))latest=r.sequence;
    }
    allocated=latest;return true;
  }
  bool readSequence(uint32_t sequence,Record &r){
    if(!sequence)return false;
    if(!flash.read(slot(sequence)*sizeof(r),&r,sizeof(r))){healthy=false;return false;}
    return valid(r,slot(sequence))&&r.sequence==sequence;
  }
  uint32_t latestSequence()const{return latest;}
  bool ok()const{return healthy;}
  bool append(Record &r){
    if(!healthy)return false;
    for(uint32_t skipped=0;skipped<kSlots;++skipped){
      uint32_t candidate=next(allocated),index=slot(candidate),offset=index*sizeof(r);
      // Only the next sector is erased, never a partly programmed current sector.
      if(offset%kSector==0){if(!flash.erase(offset)){healthy=false;return false;}}
      Record existing;
      if(!flash.read(offset,&existing,sizeof(existing))){healthy=false;return false;}
      bool blank=true;const uint8_t *raw=reinterpret_cast<const uint8_t*>(&existing);
      for(size_t i=0;i<sizeof(existing);++i)if(raw[i]!=0xff){blank=false;break;}
      allocated=candidate;
      if(!blank)continue; // Includes torn writes recovered after power loss.
      r.magic=kMagic;r.sequence=candidate;r.crc=crc32(&r,offsetof(Record,crc));
      if(!flash.write(offset,&r,sizeof(r)))return false;
      Record check;if(!flash.read(offset,&check,sizeof(check))){healthy=false;return false;}
      if(memcmp(&check,&r,sizeof(r)) || !valid(check,index))return false;
      latest=candidate;return true;
    }
    healthy=false;return false;
  }
};

}
