#pragma once
#include <TelemetryState.h>
#include "StationIdentity.h"
#include "StationPicker.h"
#include "HistoryStatus.h"
#include "HistoryCompatibility.h"
#include "HistoryOperation.h"
#include "GraphHistory.h"
#include <stdio.h>
namespace HistoryFormat {
struct StatusConnection {
  Telemetry telemetry;StationIdentity identity;
  bool connected=false,pickerBack=false,scanning=false,targetSaved=false,noUpsProfile=false;
  const char *phase="SEARCHING";
};
struct StatusSnapshot {
  StatusConnection state;HistoryStats log;StationPicker::Snapshot nearby;StationPicker::Target target;
  bool historyAvailable=true;
  struct {uint32_t runtime_seconds=0;uint8_t status=0;} ups;
  struct {bool usbMounted=false;uint32_t connects=0,drops=0,heapFree=0,heapMin=0,psramFree=0,resetCode=0;} d;
  const char *productVersion="unknown",*productCommit="unknown",*boardId="waveshare-147",*panelName="";
  bool productDirty=false,rgbAvailable=false;
  int panelWidth=0,panelHeight=0;uint8_t screenCount=0;
  char protocolVersion='R';uint32_t nowMs=0;
  bool upsEnabled=false,parserPassed=false,displayReady=false,bleTaskStarted=false,historyTaskStarted=false,bleStartupFailed=false;
  bool pickerPassed=false,pickerOpen=false,targetLoaded=false,buttonPassed=false;
  uint32_t upsSent=0,upsSendFailed=0,vendorSent=0,vendorSendFailed=0,hidNotReady=0,upsGets=0,staticGets=0,buttonPresses=0;
  uint8_t displayPage=0;
};
struct StatusProvider {
  virtual bool capture(StatusSnapshot &snapshot,uint32_t now)=0;
  virtual ~StatusProvider()=default;
};
constexpr uint8_t kStatusRows=13,kStatusPrimaryRows=6;
// Exact production STATUS serializers. The first six rows are the complete
// legacy validation/publish cycle, the current-boot time anchor, and archive inventory.
inline int serializeStatus(const StatusSnapshot &frame,uint8_t index,char *out,size_t capacity){
  // A mutex timeout is Busy, not an empty/not-ready archive or a zero clock
  // anchor. Never manufacture inventory for a client's final verification.
  if(!frame.historyAvailable&&(index==4||index==5))
    return snprintf(out,capacity,"{\"history_status_busy\":true}\n");
  const auto &state=frame.state;const auto &s=state.telemetry;const auto &r=frame.ups;
  const auto &identity=state.identity;const auto &log=frame.log;const auto &target=frame.target;
  const auto &nearby=frame.nearby;const auto &d=frame.d;
  const auto upsEnabled=frame.upsEnabled;
  const auto parserPassed=frame.parserPassed;
  const auto displayReady=frame.displayReady;
  const auto bleTaskStarted=frame.bleTaskStarted;
  const auto historyTaskStarted=frame.historyTaskStarted;
  const auto bleStartupFailed=frame.bleStartupFailed;
  const auto pickerPassed=frame.pickerPassed;
  const auto pickerOpen=frame.pickerOpen;
  const auto targetLoaded=frame.targetLoaded;
  const auto buttonPassed=frame.buttonPassed;
  const auto upsSent=frame.upsSent;
  const auto upsSendFailed=frame.upsSendFailed;
  const auto vendorSent=frame.vendorSent;
  const auto vendorSendFailed=frame.vendorSendFailed;
  const auto hidNotReady=frame.hidNotReady;
  const auto upsGets=frame.upsGets;
  const auto staticGets=frame.staticGets;
  const auto buttonPresses=frame.buttonPresses;
  const auto displayPage=frame.displayPage;
  switch(index){
  case 0:return snprintf(out,capacity,"{\"adapter\":\"UPSDEV\",\"version\":\"%c\",\"usb_update\":true,\"ups_enabled\":%s,\"parser_selftest\":%s,\"display_ready\":%s,\"gatt_connected\":%s,\"fresh\":%s,\"soc\":%u,\"charge_w\":%u,\"output_w\":%u,\"runtime_s\":%lu,\"source\":\"%s\",\"source_raw\":%u,\"ac_output\":%s,\"usb_output\":%s,\"dc_output\":%s,\"valid_frames\":%lu,\"invalid_frames\":%lu,\"ups_gets\":%lu,\"static_gets\":%lu,\"ups_status\":%u}\n",
    frame.protocolVersion,upsEnabled?"true":"false",parserPassed?"true":"false",displayReady?"true":"false",state.connected?"true":"false",telemetryFresh(s,frame.nowMs)?"true":"false",s.soc,s.chargeWatts,s.outputWatts,(unsigned long)r.runtime_seconds,sourceName(s.source),s.sourceRaw,
    (s.mainFlags&2)?"true":"false",(s.mainFlags&1)?"true":"false",(s.settingsFlags&0x10)?"true":"false",(unsigned long)s.validFrames,(unsigned long)s.invalidFrames,(unsigned long)upsGets,(unsigned long)staticGets,r.status);
  case 1:return snprintf(out,capacity,"{\"station_name\":\"%s\",\"station_address\":\"%s\",\"identity_source\":\"ble_advertisement\",\"gatt_connected\":%s}\n",
    identity.name,identity.address,state.connected?"true":"false");
  case 2:return snprintf(out,capacity,"{\"picker_selftest\":%s,\"picker_open\":%s,\"picker_scanning\":%s,\"picker_highlight\":\"%s\",\"picker_back\":%s,\"target_saved\":%s,\"target_loaded\":%s,\"target_address\":\"%s\",\"connection_state\":\"%s\",\"protocol\":%u,\"telemetry_known\":%lu,\"ups_source_known\":%s}\n",
    pickerPassed?"true":"false",pickerOpen?"true":"false",state.scanning?"true":"false",nearby.selectedMAC,state.pickerBack?"true":"false",state.targetSaved?"true":"false",targetLoaded?"true":"false",target.identity.address,state.phase,s.protocol,(unsigned long)s.known,upsTelemetryFresh(s,frame.nowMs)?"true":"false");
  case 3:return snprintf(out,capacity,"{\"board\":\"%s\",\"panel\":\"%s\",\"panel_width\":%d,\"panel_height\":%d,\"rgb_available\":%s}\n",frame.boardId,frame.panelName,frame.panelWidth,frame.panelHeight,frame.rgbAvailable?"true":"false");
  case 5:return snprintf(out,capacity,"{\"history_ready\":%s,\"history_selftest\":%s,\"history_records\":%lu,\"history_recovered\":%lu,\"history_latest\":%lu,\"history_oldest\":%lu,\"history_export_version\":3,\"history_nonce_bits\":%lu,\"history_generation\":%lu,\"history_writes\":%lu,\"history_errors\":%lu,\"history_error\":\"%s\",\"history_last_write_ms\":%lu,\"history_max_write_ms\":%lu,\"history_session\":%lu,\"history_clock\":%s,\"screen\":%u,\"button_presses\":%lu}\n",
    log.ready?"true":"false",log.selfTest?"true":"false",(unsigned long)log.count,(unsigned long)log.recovered,(unsigned long)log.latest,(unsigned long)log.oldest,(unsigned long)kHistoryNonceBits,(unsigned long)log.generation,(unsigned long)log.writes,(unsigned long)log.errors,log.error,
    (unsigned long)log.lastWriteMs,(unsigned long)log.maxWriteMs,(unsigned long)log.session,log.clockSynced?"true":"false",displayPage,(unsigned long)buttonPresses);
  case 6:return snprintf(out,capacity,"{\"history_storage_capability\":\"%s\"}\n",HistoryFormat::kStorageCapability);
  case 7:return snprintf(out,capacity,"{\"product_version\":\"%s\",\"product_commit\":\"%s\",\"product_dirty\":%s,\"firmware_protocol_version\":\"%c\",\"board_identity\":\"%s\"}\n",
    frame.productVersion,frame.productCommit,frame.productDirty?"true":"false",frame.protocolVersion,frame.boardId);
  case 8:return snprintf(out,capacity,"{\"ble_task_started\":%s,\"ble_startup_failed\":%s,\"history_task_started\":%s,\"startup_maintenance_available\":true}\n",
    bleTaskStarted?"true":"false",bleStartupFailed?"true":"false",historyTaskStarted?"true":"false");
  case 9:return snprintf(out,capacity,"{\"hid_ups_sent\":%lu,\"hid_ups_failed\":%lu,\"hid_vendor_sent\":%lu,\"hid_vendor_failed\":%lu,\"hid_not_ready\":%lu}\n",
    (unsigned long)upsSent,(unsigned long)upsSendFailed,(unsigned long)vendorSent,(unsigned long)vendorSendFailed,(unsigned long)hidNotReady);
  case 10:return snprintf(out,capacity,"{\"picker_highlight\":\"%s\",\"picker_back\":%s,\"picker_index\":%d,\"picker_total\":%u}\n",
    nearby.selectedMAC,state.pickerBack?"true":"false",nearby.selectedIndex,nearby.total);
  case 4:return snprintf(out,capacity,"{\"graph_selftest\":%s,\"graph_live_records\":%lu,\"graph_live_samples\":%lu,\"graph_live_latest_s\":%lu,\"graph_refresh_ms\":1000,\"graph_scale\":\"log1p_age\",\"graph_horizon_s\":%lu}\n",
    log.graphTest?"true":"false",(unsigned long)log.liveCount,(unsigned long)log.liveSamples,(unsigned long)log.liveLatest,(unsigned long)GraphHistory::kHorizon);
  case 11:return snprintf(out,capacity,"{\"button_selftest\":%s}\n",buttonPassed?"true":"false");
  case 12:return snprintf(out,capacity,"{\"screen_count\":%u,\"diagnostic_page\":2,\"usb_mounted\":%s,\"ble_connects\":%lu,\"ble_drops\":%lu,\"heap_free\":%lu,\"heap_min\":%lu,\"psram_free\":%lu,\"reset_reason\":%lu}\n",
    frame.screenCount,d.usbMounted?"true":"false",(unsigned long)d.connects,(unsigned long)d.drops,(unsigned long)d.heapFree,(unsigned long)d.heapMin,(unsigned long)d.psramFree,(unsigned long)d.resetCode);
  default:return 0;
  }
}
}
