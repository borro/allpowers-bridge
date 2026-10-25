#include <array>
#include <cassert>
#include "HistoryRing.h"
#include "history_v1_ring.h"

struct LegacyFlash : HistoricalAHP1::Flash {
  HistoryFormat::Flash &flash;
  explicit LegacyFlash(HistoryFormat::Flash &f) : flash(f) {}
  bool read(uint32_t offset, void *p, size_t n) override { return flash.read(offset,p,n); }
  bool write(uint32_t offset,const void *p,size_t n) override { return flash.write(offset,p,n); }
  bool erase(uint32_t offset) override { return flash.erase(offset); }
};

int main() {
  std::array<uint8_t,HistoryFormat::kBytes> memory{};
  HistoryFormat::MemoryFlash flash(memory.data());
  HistoryFormat::Ring upgraded(flash);
  assert(upgraded.recover());
  for(unsigned i=0;i<129;++i) {
    auto r=HistoryFormat::example();r.magic=HistoryFormat::header(0x37f,3);
    assert(upgraded.append(r));
  }
  LegacyFlash oldFlash(flash);
  HistoricalAHP1::Ring unsupported(oldFlash);
  assert(unsupported.recover() && unsupported.latestSequence()==0);
  HistoricalAHP1::Record old{};old.soc=100;old.flags=HistoricalAHP1::Fresh;
  assert(unsupported.append(old) && old.sequence==1);
  HistoryFormat::Record check{};
  assert(!upgraded.readSequence(2,check)); // Reproduces the released writer's damage.

  // Supported rollback uses a package whose compiled, hash-bound contract reads
  // both AHP1 and AHP2. It keeps the newer physical format rather than migrating
  // storage back to an AHP1-only writer. The Python gate rejects that old writer.
  HistoryFormat::MemoryFlash fresh(memory.data());
  LegacyFlash initialFlash(fresh);
  HistoricalAHP1::Ring initial(initialFlash);
  assert(initial.recover() && initial.append(old) && old.sequence==1);
  HistoryFormat::Ring upgrade(fresh);
  assert(upgrade.recover() && upgrade.latestSequence()==1);
  for(unsigned i=0;i<129;++i) {
    auto r=HistoryFormat::example();r.magic=HistoryFormat::header(0x37f,3);
    assert(upgrade.append(r));
  }
  HistoryFormat::Ring compatibleRollback(fresh);
  assert(compatibleRollback.recover() && compatibleRollback.latestSequence()==130);
  auto r=HistoryFormat::example();r.magic=HistoryFormat::header(0x37f,3);
  assert(compatibleRollback.append(r) && r.sequence==131);
  HistoryFormat::Ring secondUpgrade(fresh);
  assert(secondUpgrade.recover() && secondUpgrade.latestSequence()==131);
  assert(secondUpgrade.append(r) && r.sequence==132);
  for(uint32_t sequence=1;sequence<=132;++sequence) {
    assert(secondUpgrade.readSequence(sequence,check));
    assert(check.soc==100);
    if(sequence==1) assert(check.magic==HistoryFormat::kMagic);
    else assert(HistoryFormat::version2(check.magic) && HistoryFormat::known(check.magic)==0x37f);
  }
}
