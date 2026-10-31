#pragma once
#include "StatusWire.h"
#include "HistoryExport.h"
#include "HistorySample.h"
#include <array>
#include <string>
namespace ArchiveContractTests {
inline void run() {
  using namespace HistoryFormat;
  {
    Record record{};record.sequence=0x12345678;record.crc=0x9abcdef0;
    assert(digestRecord(kDigestSeed,record)==0xd117dcb5u);
    assert(!newer(1,1)&&newer(2,1)&&!newer(1,2));
    assert(newer(1,UINT32_MAX)&&!newer(UINT32_MAX,1));
    record=example();record.sequence=1;record.flags=0;record.soc=255;
    record.crc=crc32(&record,offsetof(Record,crc));assert(valid(record,0));
    record.flags=Fresh;record.crc=crc32(&record,offsetof(Record,crc));assert(!valid(record,0));
    std::array<Record,4> rows{};
    rows[3].sequence=30;rows[0].sequence=32;rows[1].sequence=35;rows[2].sequence=99;
    Record selected{};
    assert(atOrAfter(rows.data(),3,3,4,30,selected)&&selected.sequence==30);
    assert(atOrAfter(rows.data(),3,3,4,31,selected)&&selected.sequence==32);
    assert(atOrAfter(rows.data(),3,3,4,33,selected)&&selected.sequence==35);
    assert(!atOrAfter(rows.data(),3,3,4,36,selected));
    assert(!atOrAfter(rows.data(),3,0,4,1,selected));
  }

  {
    // Metadata must keep its public fields even when no station is connected.
    // Verify both states, buffer bounds, and Busy without inventing inventory.
    StatusSnapshot snapshot;
    strcpy(snapshot.state.identity.name, "Synthetic station");
    strcpy(snapshot.state.identity.address, "02:00:00:00:00:01");
    snapshot.panelName="test panel";snapshot.panelWidth=172;snapshot.panelHeight=320;
    snapshot.productVersion="test-version";snapshot.productCommit="test-commit";
    snapshot.log.count=17;snapshot.log.session=42;snapshot.log.generation=9;
    snapshot.log.liveLatest=1234;snapshot.upsSent=91;snapshot.upsSendFailed=3;
    snapshot.nearby.selectedIndex=2;snapshot.nearby.total=4;
    snapshot.screenCount=3;snapshot.d.heapFree=123456;snapshot.d.resetCode=7;
    const std::array<const char *,13> expected{
      "\"adapter\":\"UPSDEV\"", "\"station_name\":\"Synthetic station\"",
      "\"connection_state\":\"SEARCHING\"", "\"panel_width\":172",
      "\"graph_live_latest_s\":1234", "\"history_session\":42",
      "\"history_storage_capability\":", "\"product_version\":\"test-version\"",
      "\"startup_maintenance_available\":true", "\"hid_ups_failed\":3",
      "\"picker_index\":2", "\"button_selftest\":", "\"heap_free\":123456"};
    for(bool enabled : {false,true}) {
      snapshot.state.connected=enabled;snapshot.buttonPassed=enabled;
      snapshot.log.ready=enabled;snapshot.log.clockSynced=enabled;
      for(uint8_t row=0;row<kStatusRows;++row) {
        std::array<char,2048> output{};
        const int length=serializeStatus(snapshot,row,output.data(),output.size());
        assert(length>0 && size_t(length)<output.size());
        assert(strlen(output.data())==size_t(length) && output[length-1]=='\n');
        assert(strstr(output.data(),expected[row]));
        if(row==1) assert(strstr(output.data(),enabled ? "\"gatt_connected\":true" : "\"gatt_connected\":false"));
        if(row==5) {
          assert(strstr(output.data(),"\"history_export_version\":3"));
          assert(strstr(output.data(),"\"history_nonce_bits\":128"));
          assert(strstr(output.data(),"\"history_records\":17"));
          assert(strstr(output.data(),enabled ? "\"history_clock\":true" : "\"history_clock\":false"));
        }
        if(row==11) assert(strstr(output.data(),enabled ? ":true}" : ":false}"));
        std::array<char,10> small;small.fill('#');
        assert(serializeStatus(snapshot,row,small.data()+1,8)==length);
        assert(small.front()=='#' && small.back()=='#' && small[8]=='\0');
      }
    }
    snapshot.historyAvailable=false;
    char output[2048];
    for(uint8_t row : {uint8_t(4),uint8_t(5)}) {
      assert(serializeStatus(snapshot,row,output,sizeof output)>0);
      assert(std::string(output)=="{\"history_status_busy\":true}\n");
    }
    assert(serializeStatus(snapshot,1,output,sizeof output)>0);
    assert(strstr(output,"\"station_name\":\"Synthetic station\""));
  }
  {
    StatusSnapshot snapshot;
    std::array<char, 128> buffer;
    buffer.fill('#');
    const auto before = buffer;
    for (uint8_t index : {kStatusRows, uint8_t(255)}) {
      assert(serializeStatus(snapshot, index, buffer.data(), buffer.size()) == 0);
      assert(buffer == before); // Invalid rows produce no output, including a terminator.
    }
  }
  {
    // A lying erase must not cause a second pass over the partition or writes
    // into occupied slots. Once exhausted, the ring stays unhealthy.
    struct OccupiedFlash : Flash {
      uint32_t reads = 0, erases = 0, writes = 0;
      bool read(uint32_t offset, void *data, size_t size) override {
        assert(offset < kBytes && size == sizeof(Record));
        ++reads; memset(data, 0, size); return true;
      }
      bool erase(uint32_t offset) override {
        assert(offset < kBytes && offset % kSector == 0);
        ++erases; return true;
      }
      bool write(uint32_t, const void *, size_t) override { ++writes; return false; }
    } flash;
    Ring ring(flash);
    assert(ring.recover());
    flash.reads = 0;
    auto record = example();
    assert(!ring.append(record) && !ring.ok() && ring.latestSequence() == 0);
    assert(flash.reads == kSlots && flash.erases == kBytes / kSector && flash.writes == 0);
    assert(!ring.append(record));
    assert(flash.reads == kSlots && flash.erases == kBytes / kSector && flash.writes == 0);
  }
  {
    StatusSnapshot snapshot;
    char json[3072];
    assert(serializeStatus(snapshot, 0, json, sizeof json) > 0);
    assert(strstr(json, "\"ac_output\":false") && strstr(json, "\"usb_output\":false"));
    assert(strstr(json, "\"dc_output\":false"));
  }
}
}
