#include <array>
#include <cassert>
#include "HistoryRing.h"
#include "HistoryClock.h"
#include "BootEpoch.h"
struct Storage : HistoryFormat::EpochStorage {
  uint32_t value=0; bool readFails=false, writeFails=false, torn=false;
  bool read(uint32_t &out) override { out=value; return !readFails; }
  bool write(uint32_t next) override { if(!writeFails || torn) value=next; return !writeFails; }
};
int main() {
  using namespace HistoryFormat;
  Storage storage; uint32_t epoch=0;
  assert(reserveEpoch(storage,epoch)&&epoch==1);
  assert(reserveEpoch(storage,epoch)&&epoch==2);
  storage.writeFails=storage.torn=true;
  assert(!reserveEpoch(storage,epoch)&&epoch==2&&storage.value==3);
  storage.writeFails=storage.torn=false;
  assert(reserveEpoch(storage,epoch)&&epoch==4);
  storage.readFails=true; assert(!reserveEpoch(storage,epoch)); storage.readFails=false;
  storage.value=UINT32_MAX; assert(!reserveEpoch(storage,epoch));
  {
    ClockAnchor boundary;uint32_t result=99;
    assert(!boundary.candidate(1577836799,0,0,result)&&result==99);
    assert(boundary.candidate(1577836800,0,0,result)&&result==1577836800);
    assert(!boundary.candidate(1577836800,1577836801,1577836801,result));
    assert(!boundary.candidate(1577836800,1577836800,1577836800,result));
    assert(boundary.candidate(UINT32_MAX-5,100,105,result)&&result==UINT32_MAX-105);
    assert(!boundary.candidate(UINT32_MAX-4,100,105,result));
    assert(!boundary.candidate(UINT32_MAX-5,100,106,result));
    boundary.commit(UINT32_MAX-105);
    assert(boundary.at(105)==UINT32_MAX && boundary.at(106)==0 && boundary.at(107)==0);
  }
  ClockAnchor clock; uint32_t base=0;
  assert(!clock.synced()&&!clock.at(50));
  assert(!clock.candidate(1,0,0,base));
  assert(!clock.candidate(1800000000,100,99,base));
  assert(!clock.candidate(1800000000,100,106,base));
  assert(clock.candidate(1800000000,100,105,base)); clock.commit(base);
  assert(clock.at(40)==1799999940&&clock.at(110)==1800000010);
  clock.commit(1800000999); assert(clock.at(110)==1800000010);
  assert(!clock.at(UINT32_MAX));
  ClockAnchor reboot; assert(!reboot.synced()&&!reboot.at(1));
  std::array<uint8_t,kBytes> memory{}; MemoryFlash flash(memory.data()); Ring ring(flash);
  assert(ring.recover()); auto r=example(); r.session=1; r.uptimeSeconds=20;
  assert(ring.append(r)); r.uptimeSeconds=100; r.epochSeconds=clock.at(100);r.flags|=Clock;
  assert(ring.append(r));
  Ring restored(flash); assert(restored.recover()&&restored.latestSequence()==2);
  Record saved; assert(restored.readSequence(2,saved)&&saved.epochSeconds==1800000000);
  r=example();r.session=2;r.uptimeSeconds=1;assert(restored.append(r));
  assert(restored.readSequence(1,saved)&&saved.session==1&&saved.epochSeconds==0);
  assert(restored.readSequence(3,saved)&&saved.session==2&&saved.epochSeconds==0);
  for(auto oldMagic:{uint32_t(0x31504841),uint32_t(0xa200037f)}) {
    saved.magic=oldMagic;saved.crc=crc32(&saved,28); assert(!valid(saved,slot(saved.sequence)));
    const auto before=memory;const auto erases=flash.erases;
    assert(!restored.append(saved));assert(memory==before&&flash.erases==erases);
  }
}
