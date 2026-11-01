#include <array>
#include <cassert>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include "BootGestures.h"
#include "StationPicker.h"
#include "ProfilePage.h"
#include "HidPumpBudget.h"
#include "TelemetryLabels.h"

static void deviceContracts() {
  assert(!StationPicker::sameMAC(nullptr, nullptr));
  assert(!StationPicker::sameMAC("02:00:00:00:00:01", nullptr));
  assert(!StationPicker::sameMAC(nullptr, "02:00:00:00:00:01"));
  {
    // Reserve an on-time second raw press even when debounce is polled late;
    // a late raw press must finish the first Single immediately, exactly once.
    using Event = BootGestures::Event;
    for (uint32_t base : {uint32_t(0), UINT32_C(0xffffff00)}) {
      for (uint32_t onset : {579u, 580u, 581u, 900u}) {
        BootGestures button;
        button.begin(false, base);
        auto poll = [&](bool down, uint32_t at) { return button.update(down, base + at); };
        assert(poll(true, 100) == Event::None);
        assert(poll(true, 130) == Event::None);
        assert(poll(false, 200) == Event::None);
        assert(poll(false, 230) == Event::None);
        const bool second = onset <= 230 + BootGestures::DoubleMs;
        assert(poll(true, onset) == (second ? Event::None : Event::Single));
        assert(poll(true, onset + 100) == Event::None);
        assert(poll(false, onset + 150) == Event::None);
        assert(poll(false, onset + 180) == (second ? Event::Double : Event::None));
        assert(poll(false, onset + 530) == (second ? Event::None : Event::Single));
        assert(poll(false, onset + 600) == Event::None);
      }
    }
  }
  {
    BootGestures button;
    button.begin(false, 0);
    assert(button.update(true, 10) == BootGestures::Event::None);
    assert(button.update(true, 40) == BootGestures::Event::None);
    // No polling while held: recognize exactly HoldMs at raw release.
    assert(button.update(false, 840) == BootGestures::Event::None);
    assert(button.update(false, 870) == BootGestures::Event::Hold);
    assert(button.update(false, 900) == BootGestures::Event::None);
  }
  {
    char mac[18]{};
    assert(StationPicker::canonicalizeMAC("02:00:00:00:00:FF", mac));
    assert(!strcmp(mac, "02:00:00:00:00:ff"));
    StationPicker::Catalog catalog;
    std::array<StationPicker::Entry, 8> entries{};
    for (unsigned i = 0; i < entries.size(); ++i) {
      strcpy(entries[i].identity.name, "Synthetic station");
      snprintf(entries[i].identity.address, 18, "02:00:00:00:00:%02x", i);
      assert(catalog.upsert(entries[i], 0));
    }
    for (int i = 0; i < 17; ++i) {
      assert(catalog.next());
      assert(catalog.snapshot().selectedIndex == i % 8);
    }
    assert(catalog.selectMAC(entries[6].identity.address));
    assert(catalog.snapshot(0).selectedRow == -1); // Exactly beyond this page.
    assert(catalog.snapshot(1).selectedRow == 5);
    assert(catalog.snapshot(6).selectedRow == 0);
    assert(catalog.snapshot(7).selectedRow == -1);
    assert(catalog.snapshot(8).count == 0 && catalog.snapshot(8).selectedRow == -1);
    assert(catalog.selectMAC(entries[7].identity.address));
    assert(catalog.snapshot(6).selectedRow == 1);
  }
}

int main() {
  {
    std::array<uint8_t,63> command{};memcpy(command.data(),"PAGE-R2500!!",12);
    assert(ProfilePage::decode(1,command.data(),command.size(),3)==0);
    command[12]=2;assert(ProfilePage::decode(1,command.data(),13,3)==2);
    command[12]=3;assert(ProfilePage::decode(1,command.data(),13,3)==ProfilePage::None);
    assert(ProfilePage::decode(1,command.data(),12,6)==ProfilePage::None);
    assert(ProfilePage::decode(2,command.data(),13,6)==ProfilePage::None);
    assert(ProfilePage::decode(1,nullptr,13,6)==ProfilePage::None);
    assert(ProfilePage::decode(1,command.data(),13,0)==ProfilePage::None);
    memcpy(command.data(),"BOOT-R2500!!",12);assert(ProfilePage::decode(1,command.data(),63,6)==ProfilePage::None);
  }
  for(uint32_t start:{0u,UINT32_MAX-500u}){
    HidPumpBudget time(start);
    assert(time.canContinue(start));
    assert(time.canContinue(start+HidPumpBudget::WorkMicros-1));
    assert(!time.canContinue(start+HidPumpBudget::WorkMicros));
    HidPumpBudget reports(start);
    for(unsigned i=0;i<HidPumpBudget::MaxReports;++i){assert(reports.canContinue(start));reports.attempted(true);}
    assert(!reports.canContinue(start));
    HidPumpBudget stalled(start);stalled.attempted(false);
    assert(!stalled.canContinue(start+1));
  }
  static_assert(HidPumpBudget::delayMs(true)==1&&HidPumpBudget::delayMs(false)==5);
  deviceContracts();
  assert(BootGestures::selfTest());
  assert(StationPicker::selfTest());
  assert(telemetryLabelsSelfTest());
}
