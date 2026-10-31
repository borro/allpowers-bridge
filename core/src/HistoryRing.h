#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <initializer_list>

namespace HistoryFormat {
// AHP3 only: session is the durable boot ordinal, uptime is local to that boot.
constexpr uint32_t kMagic=0xa3000000;
inline uint32_t header(uint32_t known,uint8_t model){return kMagic|(known&0xfff)|(uint32_t(model&15)<<12);}
inline bool version3(uint32_t magic){return (magic&0xffff0000)==kMagic;}
inline uint32_t known(uint32_t magic){return version3(magic)?magic&0xfff:0;}
inline uint8_t model(uint32_t magic){return version3(magic)?uint8_t((magic>>12)&15):0;}
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
  return version3(r.magic) && r.sequence && slot(r.sequence)==physicalSlot &&
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
    if(!healthy||!version3(r.magic))return false;
    for(uint32_t skipped=0;skipped<kSlots;++skipped){
      uint32_t candidate=next(allocated),index=slot(candidate),offset=index*sizeof(r);
      // Only the next sector is erased, never a partly programmed current sector.
      if(offset%kSector==0){if(!flash.erase(offset)){healthy=false;return false;}}
      Record existing;
      if(!flash.read(offset,&existing,sizeof(existing))){healthy=false;return false;}
      bool blank=true;const uint8_t *raw=reinterpret_cast<const uint8_t*>(&existing);
      for(size_t i=0;i<sizeof(existing);++i)if(raw[i]!=0xff){blank=false;break;}
      allocated=candidate;
      if(blank){ // Occupied slots include torn writes recovered after power loss.
        r.sequence=candidate;r.crc=crc32(&r,offsetof(Record,crc));
        if(!flash.write(offset,&r,sizeof(r)))return false;
        Record check;if(!flash.read(offset,&check,sizeof(check))){healthy=false;return false;}
        if(memcmp(&check,&r,sizeof(r)) || !valid(check,index))return false;
        latest=candidate;return true;
      }
    }
    healthy=false;return false;
  }
};

// Runs against RAM only; never touches a physical partition.
class MemoryFlash:public Flash {
public:
  uint8_t *bytes;int partial=-1,erasePrefix=-1;bool failErase=false;uint32_t erases=0;
  explicit MemoryFlash(uint8_t *p):bytes(p){memset(bytes,0xff,kBytes);}
  bool read(uint32_t off,void *p,size_t n)override{if(off+n>kBytes)return false;memcpy(p,bytes+off,n);return true;}
  bool write(uint32_t off,const void *p,size_t n)override{
    if(off+n>kBytes)return false;size_t count=partial<0?n:size_t(partial);if(count>n)count=n;
    auto *src=static_cast<const uint8_t*>(p);
    for(size_t i=0;i<count;++i){if((bytes[off+i]&src[i])!=src[i])return false;bytes[off+i]&=src[i];}
    bool ok=partial<0;partial=-1;return ok;
  }
  bool erase(uint32_t off)override{
    if(failErase||off%kSector||off+kSector>kBytes)return false;
    if(erasePrefix>=0){memset(bytes+off,0xff,size_t(erasePrefix));erasePrefix=-1;return false;}
    memset(bytes+off,0xff,kSector);erases++;return true;
  }
};
inline Record example(){Record r={};r.magic=header(0x37f,3);r.session=1;r.soc=100;r.flags=Fresh|Mains;r.outputWatts=230;r.runtimeMinutes=38400;return r;}
inline bool selfTest(uint8_t *memory){
  if(crc32("123456789",9)!=0xcbf43926)return false;
  for(int torn=0;torn<=32;++torn){
    MemoryFlash f(memory);Ring ring(f);if(!ring.recover()||ring.latestSequence()!=0)return false;
    Record r=example();if(!ring.append(r))return false;
    f.partial=torn;if(ring.append(r))return false;
    Ring restored(f);if(!restored.recover())return false;
    if(!restored.append(r)||r.sequence<2||r.sequence>3)return false;
    Record check;if(!restored.readSequence(r.sequence,check)||check.soc!=100)return false;
  }
  // Power loss at the first slot of a new sector preserves the previous sector.
  for(int torn=0;torn<=32;++torn){
    MemoryFlash f(memory);Ring ring(f);if(!ring.recover())return false;Record r=example();
    for(unsigned i=0;i<kSector/32;++i)if(!ring.append(r))return false;
    f.partial=torn;if(ring.append(r))return false;
    Ring restored(f);Record check;
    if(!restored.recover()||!restored.readSequence(128,check)||!restored.append(r))return false;
    if(r.sequence<129||r.sequence>130||!restored.readSequence(128,check))return false;
  }
  // Interrupted erase on wrap is retried without erasing the latest sector.
  for(int prefix:{0,1,31,2048,4096}){
    MemoryFlash f(memory);Ring ring(f);if(!ring.recover())return false;Record r=example();
    for(uint32_t i=0;i<kSlots;++i)if(!ring.append(r))return false;
    f.erasePrefix=prefix;if(ring.append(r))return false;
    Ring restored(f);Record check;
    if(!restored.recover()||restored.latestSequence()!=kSlots||!restored.append(r)||r.sequence!=kSlots+1)return false;
    if(!restored.readSequence(kSlots,check))return false;
  }
  MemoryFlash f(memory);Ring ring(f);if(!ring.recover())return false;
  Record r=example();for(uint32_t i=0;i<kSlots+3;++i){r.uptimeSeconds=i*60;if(!ring.append(r))return false;}
  Ring restored(f);if(!restored.recover()||restored.latestSequence()!=kSlots+3)return false;
  Record check;if(restored.readSequence(1,check)||!restored.readSequence(kSlots+3,check))return false;
  // Overflow ordering skips reserved sequence zero and starts a fresh sector.
  memset(memory,0xff,kBytes);r=example();r.magic=kMagic;r.sequence=0xffffffff;r.crc=crc32(&r,28);
  if(!f.write(slot(r.sequence)*32,&r,32))return false;
  Ring wrap(f);if(!wrap.recover()||!wrap.append(r)||r.sequence!=1)return false;
  Ring wrapReload(f);if(!wrapReload.recover()||wrapReload.latestSequence()!=1)return false;
  // An erase failure stops all future writes for this instance.
  MemoryFlash bad(memory);Ring failed(bad);if(!failed.recover())return false;bad.failErase=true;
  return !failed.append(r)&&!failed.ok();
}
}
