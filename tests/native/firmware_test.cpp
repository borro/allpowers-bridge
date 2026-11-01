#include <array>
#include <cassert>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include "BootGestures.h"
#include "HidPumpBudget.h"
#include "DisplayFrame.h"
#include "DisplayTiles.h"
#include "PanelWindow.h"
#include "display_tiles_test.h"
#include "StationPicker.h"
#include "HistoryRing.h"
#include "GraphHistory.h"
#include "GraphSnapshot.h"
#include "ProfilePage.h"
#include "graph_snapshot_test.h"
#include "GraphRenderer.h"
#include "FrameRaster.h"
#include "graph_renderer_test.h"
#include "HistoryWire.h"
#include "TelemetryLabels.h"
#include "HistoryExport.h"

static void deviceContracts() {
  using namespace HistoryFormat;
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
  {
    auto a = example(), b = a;
    a.sequence = 1; b.sequence = 2;
    b.uptimeSeconds = 90;
    assert(GraphHistory::join(a, b));
    b.uptimeSeconds = 91;
    assert(!GraphHistory::join(a, b));
    b.uptimeSeconds = 0;
    assert(GraphHistory::join(a, b));
    a.flags &= ~Fresh;
    assert(!GraphHistory::join(a, b));
    std::array<GraphHistory::Point, 4> points{};
    for (auto &p : points) {
      p.sample = example(); p.sample.uptimeSeconds = 10;
      p.joinPrevious = p.unknownBefore = true;
    }
    assert(!GraphHistory::assignAges(points.data(), 2, 10, 1));
    assert(points[0].age == 0 && points[1].age == 0);
    assert(!points[0].unknownBefore && !points[1].unknownBefore);
    assert(points[2].unknownBefore && points[2].joinPrevious); // Count is a bound.
    points[1].sample.session = 2;
    assert(GraphHistory::assignAges(points.data(), 2, 15, 1));
    assert(points[1].unknownBefore);
    for (auto &p : points) { p.sample.flags |= Clock; p.sample.epochSeconds = 100; }
    assert(!GraphHistory::assignAges(points.data(), 2, 15, 2));
    assert(points[0].age == 5 && points[1].age == 5);
    points[1].sample.epochSeconds = 200;
    for (unsigned missing : {0u, 1u}) {
      points[missing].sample.flags &= ~Clock;
      assert(GraphHistory::assignAges(points.data(), 2, 15, 2));
      assert(points[0].age == 15 && points[1].unknownBefore);
      points[missing].sample.flags |= Clock;
    }
    points[0].sample = points[1].sample = example();
    points[1].sample.uptimeSeconds = 10;
    assert(!GraphHistory::assignAges(points.data(), 2, GraphHistory::kHorizon, 1));
    assert(points[0].age == GraphHistory::kHorizon);
    points[0].sample.session = 2;
    assert(GraphHistory::assignAges(points.data(), 1, 15, 1));
    assert(points[0].unknownBefore && points[0].age == 15);
  }
}

template <unsigned W, unsigned H, bool Little> static void displayFrames() {
  DisplayFrame<W,H,Little> frame;
  std::vector<uint16_t> pixels(W*H),shadow(W*H,0xdead),panel(W*H,0xbeef),expected(W*H);
  unsigned windows=0;
  for(unsigned i=0;i<W*H;++i)pixels[i]=uint16_t(i*37+11);
  auto present=[&](bool rotated,bool buffered=true){
    windows=0;
    // Independent forward coordinate transform of the logical image.
    const unsigned width=rotated?H:W,height=rotated?W:H;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
      uint16_t c=pixels[y*width+x];if(!Little)c=uint16_t(c<<8|c>>8);
      expected[rotated?x*W+(W-1-y):y*W+x]=c;
    }
    unsigned written=0,end=0;
    const unsigned count=frame.present(pixels.data(),width,buffered?shadow.data():nullptr,
      [&](unsigned first,unsigned rows,const uint16_t *data){
        ++windows;
        assert(rows>0&&rows<=8&&first>=end&&first+rows<=H);end=first+rows;
        for(unsigned i=0;i<rows*W;++i){assert(data[i]==expected[first*W+i]);panel[first*W+i]=data[i];}
        written+=rows;
      });
    assert(count==written&&panel==expected);
    if(buffered)assert(shadow==expected);
    return written;
  };
  assert(present(false)==H);assert(windows==(H+7)/8);assert(present(false)==0);
  pixels[0]^=0x07e0;assert(present(false)==1);
  pixels[(H-1)*W+W-1]=0;assert(present(false)==1);
  for(unsigned y=0;y<9;++y)pixels[y*W]^=0xffff;
  assert(present(false)==9);assert(present(false)==0);
  for(unsigned y=0;y<H;y+=2)pixels[y*W]^=0xf800;
  assert(present(false)==(H+1)/2);
  present(true);assert(present(true)==0);
  pixels[0]^=0xffff;assert(present(true)==1);
  pixels[W*H-1]^=0x1234;assert(present(true)==1);
  present(false);assert(present(false)==0);
  assert(present(false,false)==H);assert(present(false,false)==H);
  assert(present(false)==H);assert(present(false)==0);
  // Sparse changes, clearing and page/rotation transitions must reconstruct the
  // entire correct panel, including stripes that end at the last partial row.
  uint32_t random=12345;
  for(unsigned run=0;run<40;++run){
    for(unsigned i=0;i<run%17;++i){random=random*1664525u+1013904223u;pixels[random%(W*H)]=uint16_t(random>>16);}
    present(run%3==0);assert(present(run%3==0)==0);
  }
}

int main() {
  GraphSnapshotTests::run();
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
  DisplayTilesTests::run<172,320,true>();
  DisplayTilesTests::run<80,160,false>();
  DisplayTilesTests::run<5,19,true>();
  DisplayTilesTests::run<5,19,false>();
  DisplayTilesTests::highAddresses();
  GraphRendererTests::run();
  displayFrames<172,320,true>();
  displayFrames<80,160,false>();
  displayFrames<5,19,true>();
  displayFrames<5,19,false>();
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
  std::array<HistoryFormat::Record, GraphHistory::kCapacity> graph{};
  assert(GraphHistory::selfTest(graph.data()));
}
