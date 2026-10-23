#include <Arduino.h>
#include "USB.h"
#include "USBHID.h"
#include "esp32-hal-tinyusb.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include "Telemetry.h"
#include "UpsDescriptor.h"
#include "Display.h"
#include "esp32-hal-rgb-led.h"
#include "esp_hid_common.h"
#include "BootGestures.h"
#include "TargetPreferences.h"
#include "esp_system.h"

#include "BuildVersion.h"
#if ARDUINO_USB_MODE != 0 || ARDUINO_USB_CDC_ON_BOOT
#error Use TinyUSB OTG with manual CDC; register all HID collections before USB starts.
#endif
static portMUX_TYPE dataLock = portMUX_INITIALIZER_UNLOCKED;
static Telemetry telemetry;
static UpsReport lastValidUps={};
static StationIdentity stationIdentity;
static uint32_t lastValidAt=0;
static bool bleReady=false, connected=false;
static int stationRssi=-127;
static uint32_t scanRounds=0, advertisements=0;
static volatile bool requestBoot=false, requestRestart=false;
static bool usbStarted=false, upsEnabled=false, displayReady=false, parserPassed=false, hidParserPassed=false;
static volatile uint32_t upsGets=0, staticGets=0;
static uint32_t upsSent=0,upsSendFailed=0,vendorSent=0,vendorSendFailed=0,hidNotReady=0;
static uint32_t bleConnects=0,bleDrops=0;
static USBCDC Console;
static USBHID hid;
static Dashboard screen;
static HistoryStore history;
static BootGestures button;
static uint8_t displayPage=0;
static bool displayDirty=false;
static uint32_t buttonPresses=0;
static bool buttonPassed=false;
static uint32_t dumpNext=0,dumpRemaining=0;
static StationPicker::Catalog deviceCatalog;
static StationPicker::Target requestedTarget;
static uint32_t targetRevision=0,activeRevision=0;
static uint32_t activeConnectionToken=0;
static BLERemoteCharacteristic *activeRx=nullptr;
static AllpowersModels::Model activeModel=AllpowersModels::Model::R2500V2;
static bool pickerOpen=false,pickerBack=false,scanRequested=false,scanning=false;
static bool targetSaved=false,targetLoaded=false,pickerPassed=false,noUpsProfile=false;
static const char *connectionState="SEARCHING";
static SemaphoreHandle_t settingsOperation=nullptr;
static std::atomic<bool> historyBoundary{false};
static DiagnosticData diagnosticsSnapshot();
static StationPicker::Snapshot menuSnapshot(){
  portENTER_CRITICAL(&dataLock);auto first=deviceCatalog.snapshot();
  uint8_t selected=pickerBack?first.total:first.selectedIndex<0?0:uint8_t(first.selectedIndex);
  auto page=deviceCatalog.snapshot((selected/3)*3);portEXIT_CRITICAL(&dataLock);return page;
}
static void drawScreen(){
  if(pickerOpen)screen.renderPicker(menuSnapshot(),pickerBack,scanning,connectionState,connected?identitySnapshot():StationIdentity{});
  else if(displayPage==1)screen.renderHistory(history,snapshot());
  else if(displayPage>=2)screen.renderDiagnostics(diagnosticsSnapshot(),displayPage-2);
  else screen.render(snapshot(),identitySnapshot(),connected,millis());
}
static void led(uint8_t r,uint8_t g,uint8_t b){if constexpr(Board::rgb>=0)rgbLedWriteOrdered(Board::rgb,LED_COLOR_ORDER_RGB,r,g,b);}
static void updateLed(const Telemetry &s) {
  if(requestBoot){led(12,0,12);return;}
  if(!telemetryFresh(s,millis())){
    if(s.validFrames || !parserPassed)led(16,0,0);
    else led(0,0,12);
  }else if(s.source==PowerSource::Mains)led(0,12,0);
  else if(s.source==PowerSource::Battery)led(12,8,0);
  else led(0,8,8);
}
static bool descriptorSelfTest(){
  auto *map=esp_hid_parse_report_map(kUpsDescriptor,sizeof(kUpsDescriptor));if(!map)return false;
  bool input=false,feature=false,stat=false;
  for(uint8_t i=0;i<map->reports_len;++i){auto &r=map->reports[i];
    if(r.protocol_mode!=ESP_HID_PROTOCOL_MODE_REPORT)continue;
    if(r.report_id==2 && r.value_len==6){
      if(r.report_type==ESP_HID_REPORT_TYPE_INPUT)input=true;
      if(r.report_type==ESP_HID_REPORT_TYPE_FEATURE)feature=true;
    }
    if(r.report_id==3 && r.report_type==ESP_HID_REPORT_TYPE_FEATURE && r.value_len==9)stat=true;
  }
  esp_hid_free_report_map(map);return input&&feature&&stat;
}
static StationPicker::Target targetSnapshot(uint32_t &revision){
  portENTER_CRITICAL(&dataLock);auto copy=requestedTarget;revision=targetRevision;portEXIT_CRITICAL(&dataLock);return copy;
}
static bool settingsIdle(){
  if(!settingsOperation)return true;
  if(xSemaphoreTake(settingsOperation,0)!=pdTRUE)return false;
  xSemaphoreGive(settingsOperation);return true;
}
static void loadLastTarget(){
  settingsOperation=xSemaphoreCreateMutex();
  TargetPreferences backend;
  if(backend.begin(true)){StationPicker::Target saved;StationPicker::TargetStorage storage(backend);
    if(storage.load(saved)){requestedTarget=saved;targetLoaded=true;targetSaved=true;}
    backend.end();
  }
  if(!targetLoaded){pickerOpen=true;scanRequested=true;pickerBack=true;connectionState="SELECT DEVICE";}
}
static void rememberTarget(const StationPicker::Target &target,uint32_t revision){
  if(!settingsOperation||requestBoot||requestRestart)return;
  xSemaphoreTake(settingsOperation,portMAX_DELAY);
  bool saved=false;
  uint32_t currentRevision;targetSnapshot(currentRevision);
  if(currentRevision==revision&&!requestBoot&&!requestRestart){TargetPreferences backend;
    if(backend.begin(false)){StationPicker::TargetStorage storage(backend);saved=storage.save(target);backend.end();}
  }
  portENTER_CRITICAL(&dataLock);if(targetRevision==revision)targetSaved=saved;portEXIT_CRITICAL(&dataLock);
  xSemaphoreGive(settingsOperation);
}
static void menuGesture(BootGestures::Event event){
  if(event==BootGestures::Event::None)return;
  buttonPresses++;portENTER_CRITICAL(&dataLock);
  if(!pickerOpen){
    if(event==BootGestures::Event::Single)displayPage=(displayPage+1)%Board::screenCount;
    else if(event==BootGestures::Event::Double){
      pickerOpen=true;scanRequested=true;
      deviceCatalog.prune(millis(),connected?stationIdentity.address:nullptr);
      if(!deviceCatalog.selectMAC(requestedTarget.identity.address)){deviceCatalog.clearSelection();deviceCatalog.next();}
      pickerBack=deviceCatalog.size()==0;
    }
  }else if(event==BootGestures::Event::Single){
    auto view=deviceCatalog.snapshot();
    if(pickerBack){pickerBack=false;deviceCatalog.clearSelection();if(!deviceCatalog.next())pickerBack=true;}
    else if(view.selectedIndex>=0&&view.selectedIndex+1>=view.total){pickerBack=true;deviceCatalog.clearSelection();}
    else deviceCatalog.next();
  }else{
    StationPicker::Target selected;
    if(pickerBack)pickerOpen=false;
    else if(deviceCatalog.selectedTarget(selected)){
      if(!selected.connectable)connectionState="NOT CONNECTABLE";
      else{
        pickerOpen=false;
        if(!StationPicker::sameMAC(selected.identity.address,requestedTarget.identity.address)){
          requestedTarget=selected;targetRevision++;targetSaved=false;connectionState="CONNECTING";
          activeRx=nullptr;
          telemetry.mainSeen=false;telemetry.settingsSeen=false;telemetry.source=PowerSource::Unknown;
          telemetry.lastKnownSource=PowerSource::Unknown;historyBoundary.store(true);
        }
        scanRequested=true;
      }
    }
  }
  portEXIT_CRITICAL(&dataLock);displayDirty=true;
}
static StationIdentity identitySnapshot() {
  StationIdentity copy;portENTER_CRITICAL(&dataLock);copy=stationIdentity;portEXIT_CRITICAL(&dataLock);return copy;
}
static Telemetry snapshot() {
  Telemetry copy;portENTER_CRITICAL(&dataLock);copy=telemetry;portEXIT_CRITICAL(&dataLock);return copy;
}
static UpsReport upsSnapshot() {
  Telemetry s=snapshot();bool fresh=upsTelemetryFresh(s,millis());
  if(!fresh){
    portENTER_CRITICAL(&dataLock);auto previous=lastValidUps;portEXIT_CRITICAL(&dataLock);
    previous.status=UPS_BATTERY_PRESENT|UPS_COMMUNICATION_LOST|(previous.status&UPS_AC_PRESENT);
    return previous;
  }
  UpsReport r={s.soc,uint32_t(s.minutes)*60,UPS_BATTERY_PRESENT};
  if(s.lastKnownSource==PowerSource::Mains)r.status|=UPS_AC_PRESENT;
  if(!fresh)r.status|=UPS_COMMUNICATION_LOST;
  else {
    if(s.chargeWatts>0 && s.soc<100)r.status|=UPS_CHARGING;
    if(s.source==PowerSource::Battery && s.chargeWatts==0 && s.outputWatts>0)r.status|=UPS_DISCHARGING;
    if(s.source==PowerSource::Battery && s.soc<=5)r.status|=UPS_BELOW_LIMIT;
  }
  // No power-off command and no synthetic ShutdownImminent flag.
  portENTER_CRITICAL(&dataLock);lastValidUps=r;portEXIT_CRITICAL(&dataLock);
  return r;
}
static const uint8_t vendorDescriptor[]={
  0x06,0,0xff,0x09,1,0xa1,1,0x85,1,0x15,0,0x26,0xff,0,0x75,8,0x95,63,
  0x09,2,0x81,2,0x09,3,0x91,2,0x09,4,0xb1,2,0xc0};
static ProbeReport probeSnapshot() {
  ProbeReport p={};memcpy(p.magic,"UPSDEV",6);p.version=ADAPTER_VERSION;p.uptime=millis();
  portENTER_CRITICAL(&dataLock);
  p.flags=(bleReady?1:0)|(connected?2:0)|(upsEnabled?4:0)|(parserPassed?8:0);
  p.rounds=scanRounds;p.advertisements=advertisements;p.rssi=stationRssi;
  portEXIT_CRITICAL(&dataLock);
  auto identity=identitySnapshot();
  strlcpy(p.mac,identity.address,sizeof(p.mac));strlcpy(p.name,identity.name,sizeof(p.name));return p;
}
class MaintenanceHID:public USBHIDDevice {
public:
  MaintenanceHID(){USBHID::addDevice(this,sizeof(vendorDescriptor));}
  uint16_t _onGetDescriptor(uint8_t *b)override{memcpy(b,vendorDescriptor,sizeof(vendorDescriptor));return sizeof(vendorDescriptor);}
  uint16_t _onGetFeature(uint8_t id,uint8_t *b,uint16_t n)override{
    if(id!=1)return 0;auto p=probeSnapshot();n=min(n,uint16_t(sizeof(p)));memcpy(b,&p,n);return n;
  }
  void _onOutput(uint8_t id,const uint8_t *b,uint16_t n)override{
    if(id==1 && n>=12 && !memcmp(b,"BOOT-R2500!!",12))requestBoot=true;
  }
  void _onSetFeature(uint8_t id,const uint8_t *b,uint16_t n)override{_onOutput(id,b,n);}
} maintenance;
class UpsHID:public USBHIDDevice {
public:
  bool enable(){return USBHID::addDevice(this,sizeof(kUpsDescriptor));}
  uint16_t _onGetDescriptor(uint8_t *b)override{memcpy(b,kUpsDescriptor,sizeof(kUpsDescriptor));return sizeof(kUpsDescriptor);}
  uint16_t _onGetFeature(uint8_t id,uint8_t *b,uint16_t n)override{
    if(id==2){upsGets++;auto p=upsSnapshot();n=min(n,uint16_t(sizeof(p)));memcpy(b,&p,n);return n;}
    if(id==3){staticGets++;n=min(n,uint16_t(sizeof(kUpsStatic)));memcpy(b,&kUpsStatic,n);return n;}
    return 0;
  }
} ups;
static void cdcEvent(void *,esp_event_base_t base,int32_t event,void *data) {
  if(base==ARDUINO_USB_CDC_EVENTS && event==ARDUINO_USB_CDC_LINE_CODING_EVENT &&
    static_cast<arduino_usb_cdc_event_data_t*>(data)->line_coding.bit_rate==1200)requestBoot=true;
}
static void notifyStation(BLERemoteCharacteristic *sender,uint8_t *bytes,size_t length,bool,uint32_t revision,uint32_t token) {
  portENTER_CRITICAL(&dataLock);
  if(revision!=activeRevision||revision!=targetRevision||token!=activeConnectionToken||sender!=activeRx){portEXIT_CRITICAL(&dataLock);return;}
  bool valid=decodeStationTelemetry(telemetry,bytes,length,millis(),activeModel);if(valid)lastValidAt=millis();
  portEXIT_CRITICAL(&dataLock);
  if(usbStarted && length<=256){
    char hex[513];for(size_t i=0;i<length;++i)snprintf(hex+i*2,3,"%02x",bytes[i]);hex[length*2]=0;
    Console.printf("{\"station_notification_hex\":\"%s\",\"valid\":%s}\n",hex,valid?"true":"false");
  }
}
class ClientEvents:public BLEClientCallbacks {
  void onConnect(BLEClient*)override{portENTER_CRITICAL(&dataLock);connected=true;bleConnects++;portEXIT_CRITICAL(&dataLock);}
  void onDisconnect(BLEClient*)override{
    portENTER_CRITICAL(&dataLock);connected=false;bleDrops++;activeRx=nullptr;telemetry.mainSeen=false;telemetry.settingsSeen=false;connectionState="DISCONNECTED";portEXIT_CRITICAL(&dataLock);
  }
} clientEvents;
static void statusRequest(BLERemoteCharacteristic *tx) {
  if(!tx || !(tx->canWrite()||tx->canWriteNoResponse()))return;
  auto request=AllpowersModels::readOnlyPoll(activeModel);
  if(request.length&&request.length<=16){uint8_t bytes[16];memcpy(bytes,request.bytes,request.length);tx->writeValue(bytes,request.length,tx->canWrite());}
}
static void scanNearby(BLEScan *scan,uint32_t seconds){
  portENTER_CRITICAL(&dataLock);scanning=true;scanRequested=false;portEXIT_CRITICAL(&dataLock);
  BLEScanResults *found=scan->start(seconds,false);uint32_t now=millis();
  if(found){
    portENTER_CRITICAL(&dataLock);scanRounds++;advertisements+=found->getCount();portEXIT_CRITICAL(&dataLock);
    for(int i=0;i<found->getCount();++i){auto d=found->getDevice(i);
      if(!d.isConnectable())continue;
      StationPicker::Entry e;
      auto address=d.getAddress().toString();if(!StationPicker::canonicalizeMAC(address.c_str(),e.identity.address))continue;
      String name=d.haveName()?d.getName():String("UNNAMED");
      e.model=uint8_t(AllpowersModels::nameHint(name.c_str()));
      size_t used=0;for(size_t j=0;j<name.length()&&used<sizeof(e.identity.name)-1;++j){
        unsigned char c=name[j];e.identity.name[used++]=c>=32&&c<=126&&c!='"'&&c!='\\'?char(c):'?';
      }
      if(!used)strlcpy(e.identity.name,"UNNAMED",sizeof(e.identity.name));
      e.rssi=d.getRSSI();e.addressType=d.getAddressType();e.connectable=d.isConnectable();
      portENTER_CRITICAL(&dataLock);deviceCatalog.upsert(e,now,connected?stationIdentity.address:nullptr);portEXIT_CRITICAL(&dataLock);
    }
  }
  scan->clearResults();
  portENTER_CRITICAL(&dataLock);deviceCatalog.prune(now,connected?stationIdentity.address:nullptr);scanning=false;portEXIT_CRITICAL(&dataLock);
}
static void bleTask(void*) {
  if(!BLEDevice::init("R2500 USB UPS")){vTaskDelete(nullptr);return;}
  portENTER_CRITICAL(&dataLock);bleReady=true;portEXIT_CRITICAL(&dataLock);
  BLEScan *scan=BLEDevice::getScan();scan->setActiveScan(true);scan->setInterval(160);scan->setWindow(80);
  BLEClient *client=BLEDevice::createClient();client->setClientCallbacks(&clientEvents);
  for(;;){
    if(requestBoot||requestRestart){vTaskDelay(pdMS_TO_TICKS(100));continue;}
    scanNearby(scan,3);uint32_t revision;auto target=targetSnapshot(revision);StationPicker::Entry candidate;bool found=false;
    portENTER_CRITICAL(&dataLock);
    int index=deviceCatalog.find(target.identity.address);
    if(index>=0){candidate=deviceCatalog.snapshot(uint8_t(index)).entries[0];found=candidate.connectable;}
    portEXIT_CRITICAL(&dataLock);
    if(found&&!requestBoot&&!requestRestart){
      target=StationPicker::targetOf(candidate);
      portENTER_CRITICAL(&dataLock);
      activeRevision=revision;uint32_t token=++activeConnectionToken;activeRx=nullptr;
      activeModel=AllpowersModels::Model(target.model);stationIdentity=target.identity;stationRssi=candidate.rssi;
      telemetry.mainSeen=telemetry.settingsSeen=false;telemetry.known=0;telemetry.protocol=target.model;
      noUpsProfile=activeModel!=AllpowersModels::Model::R2500V2;connectionState="CONNECTING";lastValidAt=0;
      portEXIT_CRITICAL(&dataLock);
      bool ok=client->connect(BLEAddress(target.identity.address),target.addressType,10000);
      if(ok){
        // BLEClient caches services across connections. Discover afresh so a
        // newly selected peripheral never receives the prior peer's handles.
        client->getServices();
        auto *service=client->getService(AllpowersModels::kServiceUuid);
        auto *rx=service?service->getCharacteristic(AllpowersModels::kNotifyUuid):nullptr;
        auto *tx=service?service->getCharacteristic(AllpowersModels::kWriteUuid):nullptr;
        bool subscribed=rx&&rx->canNotify();
        if(subscribed){
          portENTER_CRITICAL(&dataLock);if(revision==targetRevision)activeRx=rx;portEXIT_CRITICAL(&dataLock);
          rx->registerForNotify([revision,token](BLERemoteCharacteristic *sender,uint8_t *bytes,size_t n,bool notify){notifyStation(sender,bytes,n,notify,revision,token);});
          vTaskDelay(pdMS_TO_TICKS(500));statusRequest(tx);
        }
        portENTER_CRITICAL(&dataLock);connectionState=subscribed?"CONNECTED":"NO TELEMETRY";
        if(!subscribed)noUpsProfile=true;
        portEXIT_CRITICAL(&dataLock);
        uint32_t currentRevision;targetSnapshot(currentRevision);
        if(currentRevision==revision&&!requestBoot&&!requestRestart)rememberTarget(target,revision);
        uint32_t since=millis(),keepalive=since,lastScan=since;
        while(client->isConnected()){
          vTaskDelay(pdMS_TO_TICKS(100));uint32_t now=millis(),last,current;
          portENTER_CRITICAL(&dataLock);last=lastValidAt;current=targetRevision;
          bool rescan=scanRequested||(pickerOpen&&uint32_t(now-lastScan)>=8000);portEXIT_CRITICAL(&dataLock);
          if(current!=revision){client->disconnect();break;}
          if(requestBoot||requestRestart)continue;
          if(rescan){scanNearby(scan,2);lastScan=millis();}
          if(subscribed&&uint32_t(now-keepalive)>=AllpowersCore::kPollIntervalMs){statusRequest(tx);keepalive=now;}
          if(subscribed&&uint32_t(now-since)>AllpowersCore::kReconnectSilenceMs&&uint32_t(now-last)>AllpowersCore::kReconnectSilenceMs){client->disconnect();break;}
        }
      }else{portENTER_CRITICAL(&dataLock);connectionState="CONNECT FAILED";portEXIT_CRITICAL(&dataLock);}
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
static const char *resetReasonName(){
  switch(esp_reset_reason()){
    case ESP_RST_POWERON:return "POWERON";case ESP_RST_EXT:return "EXT";case ESP_RST_SW:return "SOFTWARE";
    case ESP_RST_PANIC:return "PANIC";case ESP_RST_INT_WDT:return "INT WDT";case ESP_RST_TASK_WDT:return "TASK WDT";
    case ESP_RST_WDT:return "WATCHDOG";case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";case ESP_RST_BROWNOUT:return "BROWNOUT";
    default:return "OTHER";
  }
}
static DiagnosticData diagnosticsSnapshot(){
  DiagnosticData d;d.version=ADAPTER_VERSION;
  portENTER_CRITICAL(&dataLock);
  d.telemetry=telemetry;d.station=stationIdentity;d.bleConnected=connected;d.scanRssi=stationRssi;
  d.connects=bleConnects;d.drops=bleDrops;strlcpy(d.phase,connectionState,sizeof(d.phase));
  portEXIT_CRITICAL(&dataLock);
  d.nowMs=millis();d.uptimeSeconds=uint64_t(esp_timer_get_time())/1000000;
  d.log=history.status();d.reset=resetReasonName();d.resetCode=uint32_t(esp_reset_reason());
  d.usbMounted=bool(USB);d.cdcOpen=bool(Console);d.upsEnabled=upsEnabled;
  d.upsGets=upsGets;d.upsSent=upsSent;d.upsFailed=upsSendFailed;
  d.heapFree=ESP.getFreeHeap();d.heapMin=ESP.getMinFreeHeap();d.psramFree=ESP.getFreePsram();return d;
}
static void printDiagnostics(){
  auto d=diagnosticsSnapshot();auto view=diagnosticView(d);char body[2400];
  size_t used=snprintf(body,sizeof(body),"{\"diagnostic\":true,\"version\":\"%c\",\"page\":%u,\"heading\":\"%s\",\"footer\":\"%s\",\"rows\":[",ADAPTER_VERSION,displayPage,view.heading,view.footer);
  for(unsigned i=0;i<DiagnosticView::Count&&used<sizeof(body)-3;++i){auto &r=view.rows[i];
    used+=snprintf(body+used,sizeof(body)-used,"%s{\"label\":\"%s\",\"value\":\"%s\",\"tone\":%u}",i?",":"",r.label,r.value,unsigned(r.tone));
  }
  if(used<sizeof(body)-3){memcpy(body+used,"]}\n",3);Console.write(reinterpret_cast<uint8_t*>(body),used+3);}
}
static void printStatus() {
  Telemetry s=snapshot();auto r=upsSnapshot();
  Console.printf("{\"hid_ups_sent\":%lu,\"hid_ups_failed\":%lu,\"hid_vendor_sent\":%lu,\"hid_vendor_failed\":%lu,\"hid_not_ready\":%lu}\n",
    (unsigned long)upsSent,(unsigned long)upsSendFailed,(unsigned long)vendorSent,(unsigned long)vendorSendFailed,(unsigned long)hidNotReady);
  Console.printf("{\"adapter\":\"UPSDEV\",\"version\":\"%c\",\"usb_update\":true,\"ups_enabled\":%s,\"parser_selftest\":%s,\"display_ready\":%s,\"gatt_connected\":%s,\"fresh\":%s,\"soc\":%u,\"charge_w\":%u,\"output_w\":%u,\"runtime_s\":%lu,\"source\":\"%s\",\"source_raw\":%u,\"ac_output\":%s,\"usb_output\":%s,\"dc_output\":%s,\"valid_frames\":%lu,\"invalid_frames\":%lu,\"ups_gets\":%lu,\"static_gets\":%lu,\"ups_status\":%u}\n",
    ADAPTER_VERSION,upsEnabled?"true":"false",parserPassed?"true":"false",displayReady?"true":"false",connected?"true":"false",telemetryFresh(s,millis())?"true":"false",s.soc,s.chargeWatts,s.outputWatts,(unsigned long)r.runtime_seconds,sourceName(s.source),s.sourceRaw,
    (s.mainFlags&2)?"true":"false",(s.mainFlags&1)?"true":"false",(s.settingsFlags&0x10)?"true":"false",(unsigned long)s.validFrames,(unsigned long)s.invalidFrames,(unsigned long)upsGets,(unsigned long)staticGets,r.status);
  auto identity=identitySnapshot();
  Console.printf("{\"station_name\":\"%s\",\"station_address\":\"%s\",\"identity_source\":\"ble_advertisement\",\"gatt_connected\":%s}\n",
    identity.name,identity.address,connected?"true":"false");
  auto log=history.status();
  uint32_t revision;auto target=targetSnapshot(revision);
  portENTER_CRITICAL(&dataLock);auto nearby=deviceCatalog.snapshot();portEXIT_CRITICAL(&dataLock);
  Console.printf("{\"picker_highlight\":\"%s\",\"picker_back\":%s,\"picker_index\":%d,\"picker_total\":%u}\n",
    nearby.selectedMAC,pickerBack?"true":"false",nearby.selectedIndex,nearby.total);
  Console.printf("{\"picker_selftest\":%s,\"picker_open\":%s,\"picker_scanning\":%s,\"picker_highlight\":\"%s\",\"picker_back\":%s,\"target_saved\":%s,\"target_loaded\":%s,\"target_address\":\"%s\",\"connection_state\":\"%s\",\"protocol\":%u,\"telemetry_known\":%lu,\"ups_source_known\":%s}\n",
    pickerPassed?"true":"false",pickerOpen?"true":"false",scanning?"true":"false",nearby.selectedMAC,pickerBack?"true":"false",targetSaved?"true":"false",targetLoaded?"true":"false",target.identity.address,connectionState,s.protocol,(unsigned long)s.known,upsTelemetryFresh(s,millis())?"true":"false");
  Console.printf("{\"graph_selftest\":%s,\"graph_live_records\":%lu,\"graph_live_samples\":%lu,\"graph_live_latest_s\":%lu,\"graph_refresh_ms\":1000,\"graph_scale\":\"log1p_age\",\"graph_horizon_s\":%lu}\n",
    log.graphTest?"true":"false",(unsigned long)log.liveCount,(unsigned long)log.liveSamples,(unsigned long)log.liveLatest,(unsigned long)GraphHistory::kHorizon);
  Console.printf("{\"button_selftest\":%s}\n",buttonPassed?"true":"false");
  Console.printf("{\"history_ready\":%s,\"history_selftest\":%s,\"history_records\":%lu,\"history_recovered\":%lu,\"history_latest\":%lu,\"history_writes\":%lu,\"history_errors\":%lu,\"history_error\":\"%s\",\"history_last_write_ms\":%lu,\"history_max_write_ms\":%lu,\"history_session\":%lu,\"history_clock\":%s,\"screen\":%u,\"button_presses\":%lu}\n",
    log.ready?"true":"false",log.selfTest?"true":"false",(unsigned long)log.count,(unsigned long)log.recovered,(unsigned long)log.latest,(unsigned long)log.writes,(unsigned long)log.errors,log.error,
    (unsigned long)log.lastWriteMs,(unsigned long)log.maxWriteMs,(unsigned long)log.session,log.clockSynced?"true":"false",displayPage,(unsigned long)buttonPresses);
  Console.printf("{\"board\":\"%s\",\"panel\":\"%s\",\"panel_width\":%d,\"panel_height\":%d,\"rgb_available\":%s}\n",Board::id,Board::panelName,Board::width,Board::height,Board::rgb>=0?"true":"false");
  auto d=diagnosticsSnapshot();
  Console.printf("{\"screen_count\":%u,\"diagnostic_page\":2,\"usb_mounted\":%s,\"ble_connects\":%lu,\"ble_drops\":%lu,\"heap_free\":%lu,\"heap_min\":%lu,\"psram_free\":%lu,\"reset_reason\":%lu}\n",
    Board::screenCount,d.usbMounted?"true":"false",(unsigned long)d.connects,(unsigned long)d.drops,(unsigned long)d.heapFree,(unsigned long)d.heapMin,(unsigned long)d.psramFree,(unsigned long)d.resetCode);
}
static void historyTask(void*){
  history.initialize();uint64_t lastSample=uint64_t(esp_timer_get_time())/1000000,lastLive=0;
  bool attempted=false,minuteGap=false,liveGap=false,liveStarted=false;
  for(;;){
    if(requestBoot||requestRestart){history.requestPause();vTaskDelay(pdMS_TO_TICKS(100));continue;}
    Telemetry s=snapshot();bool fresh=telemetryFresh(s,millis());if(!fresh){minuteGap=true;liveGap=true;}
    if(historyBoundary.exchange(false)){minuteGap=true;liveGap=true;}
    uint64_t now=uint64_t(esp_timer_get_time())/1000000;
    if(now!=lastLive&&(fresh||liveStarted)){
      history.recordLive(s,liveGap||(liveStarted&&now-lastLive>1));lastLive=now;liveStarted=true;liveGap=false;
    }
    if((!attempted&&fresh)||now-lastSample>=60){
      attempted=true;if(history.record(s,minuteGap))minuteGap=false;lastSample=now;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
static void printDevices(){
  StationPicker::Snapshot pages[4];
  portENTER_CRITICAL(&dataLock);for(int i=0;i<4;++i)pages[i]=deviceCatalog.snapshot(i*6);portEXIT_CRITICAL(&dataLock);
  Console.printf("{\"device_count\":%u,\"picker_open\":%s,\"scanning\":%s}\n",pages[0].total,pickerOpen?"true":"false",scanning?"true":"false");
  for(auto &page:pages)for(int i=0;i<page.count;++i){auto &e=page.entries[i];
    Console.printf("{\"ble_device\":true,\"name\":\"%s\",\"address\":\"%s\",\"rssi\":%d,\"connectable\":%s,\"profile\":%u,\"seen_age_ms\":%lu}\n",
      e.identity.name,e.identity.address,e.rssi,e.connectable?"true":"false",e.model,(unsigned long)(millis()-e.seenAt));
  }
  Console.println("{\"devices_done\":true}");
}
static void streamHistory(){
  if(!dumpRemaining)return;HistoryFormat::Record r;
  bool found=history.readSequence(dumpNext,r);dumpNext=HistoryFormat::next(dumpNext);dumpRemaining--;
  if(found)Console.printf("{\"history_record\":true,\"sequence\":%lu,\"session\":%lu,\"uptime_s\":%lu,\"epoch\":%lu,\"soc\":%u,\"charge_w\":%u,\"output_w\":%u,\"runtime_minutes\":%u,\"flags\":%u,\"crc\":%lu}\n",
    (unsigned long)r.sequence,(unsigned long)r.session,(unsigned long)r.uptimeSeconds,(unsigned long)r.epochSeconds,r.soc,r.chargeWatts,r.outputWatts,r.runtimeMinutes,r.flags,(unsigned long)r.crc);
  if(!dumpRemaining)Console.println("{\"history_dump_done\":true}");
}
static void startUsb(bool withUps) {
  // Prime the retained report before USB can request it. If BLE disappeared
  // during startup, enumerate maintenance rather than an uninitialised UPS.
  if(withUps){auto initial=upsSnapshot();withUps=!(initial.status&UPS_COMMUNICATION_LOST);}
  hidParserPassed=descriptorSelfTest();upsEnabled=withUps && hidParserPassed && ups.enable();
  USB.productName(withUps?"ALLPOWERS R2500 V2 USB UPS":"ALLPOWERS BLE adapter");USB.manufacturerName("Local ALLPOWERS adapter");
  char serialNumber[32];uint64_t chipMac=ESP.getEfuseMac();
  snprintf(serialNumber,sizeof(serialNumber),"R2500-%02X%02X%02X%02X%02X%02X",unsigned(chipMac&255),unsigned(chipMac>>8&255),unsigned(chipMac>>16&255),unsigned(chipMac>>24&255),unsigned(chipMac>>32&255),unsigned(chipMac>>40&255));
  USB.serialNumber(serialNumber);USB.PID(0x4002); // Lab development identity.
  USB.firmwareVersion(0x0101+ADAPTER_VERSION-'A');
  Console.begin(115200);Console.enableReboot(false);Console.onEvent(cdcEvent);Console.setTxTimeoutMs(10);
  hid.begin();USB.begin();usbStarted=true;
}
void setup() {
  buttonPassed=BootGestures::selfTest();pickerPassed=StationPicker::selfTest()&&AllpowersModels::selfTest();
  loadLastTarget();pinMode(Board::boot,INPUT_PULLUP);button.begin(digitalRead(Board::boot)==LOW,millis());
  led(0,0,12); // Optional board-specific RGB; absent on Pocket.
  parserPassed=telemetrySelfTest()&&pickerPassed;displayReady=screen.begin();
  if(parserPassed)xTaskCreatePinnedToCore(bleTask,"station_ble",10240,nullptr,1,nullptr,0);
  uint32_t start=millis();
  // Never enumerate a battery with fabricated zero SOC before station data arrives.
  // A bounded timeout always exposes maintenance CDC/vendor HID for recovery.
  while(targetLoaded&&parserPassed&&!upsTelemetryFresh(snapshot(),millis())&&!(connected&&noUpsProfile)&&uint32_t(millis()-start)<25000){
    updateLed(snapshot());screen.render(snapshot(),identitySnapshot(),connected,millis());delay(200);
  }
  startUsb(parserPassed&&upsTelemetryFresh(snapshot(),millis()));
  if(history.begin())xTaskCreatePinnedToCore(historyTask,"history_log",6144,nullptr,1,nullptr,1);
}
void loop() {
  static char command[40];static size_t used=0;static uint32_t lastReport=0,bootDeadline=0;
  if(buttonPassed)menuGesture(button.update(digitalRead(Board::boot)==LOW,millis()));
  while(Console.available()){
    char c=Console.read();if(c=='\n'){
      command[used]=0;
      if(!strcmp(command,"BOOT-R2500!!"))requestBoot=true;
      else if(!strcmp(command,"STATUS"))printStatus();
      else if(!strcmp(command,"RETRY-USB"))requestRestart=true;
      else if(!strcmp(command,"HISTORY"))printStatus();
      else if(!strcmp(command,"DIAG"))printDiagnostics();
      else if(!strcmp(command,"DEVICES"))printDevices();
      else if(!strcmp(command,"PICKER")){if(!pickerOpen)menuGesture(BootGestures::Event::Double);}
      else if(!strcmp(command,"NEXT")){if(pickerOpen)menuGesture(BootGestures::Event::Single);}
      else if(!strcmp(command,"SELECT")){if(pickerOpen)menuGesture(BootGestures::Event::Double);}
      else if(!strcmp(command,"PICKER-CLOSE")){pickerOpen=false;displayDirty=true;}
      else if(!strncmp(command,"SCREEN ",7)&&command[7]>='0'&&command[7]<'0'+Board::screenCount&&!command[8]){
        displayPage=command[7]-'0';displayDirty=true;printStatus();
      }else if(!strncmp(command,"TIME ",5)){
        char *end=nullptr;unsigned long epoch=strtoul(command+5,&end,10);
        if(end&&!*end&&epoch>=1577836800UL&&epoch<=4102444800UL){history.setTime(epoch);Console.println("{\"time_set\":true}");}
      }else if(!strncmp(command,"HISTORY-READ ",13)){
        unsigned long first=0,count=0;if(sscanf(command+13,"%lu %lu",&first,&count)==2&&first&&count&&count<=HistoryFormat::kKeep){
          dumpNext=uint32_t(first);dumpRemaining=uint32_t(count);
        }
      }
      used=0;
    }else if(c!='\r'){if(used<sizeof(command)-1)command[used++]=c;else used=0;}
  }
  if(requestBoot||requestRestart){
    history.requestPause();
    if(!bootDeadline){updateLed(snapshot());bootDeadline=millis()+150;}
    if(int32_t(millis()-bootDeadline)>=0&&history.pausedAndIdle()&&settingsIdle()){
      if(requestBoot)usb_persist_restart(RESTART_BOOTLOADER);else ESP.restart();
    }
  }
  if(targetSaved&&!pickerOpen&&connected&&digitalRead(Board::boot)==HIGH){
    if((upsEnabled&&noUpsProfile)||(!upsEnabled&&upsTelemetryFresh(snapshot(),millis())))requestRestart=true;
  }
  uint32_t now=millis();
  if(uint32_t(now-lastReport)>=1000){
    lastReport=now;printStatus();auto p=probeSnapshot();
    if(hid.ready()){
      if(hid.SendReport(1,&p,sizeof(p),5))vendorSent++;else vendorSendFailed++;
      if(upsEnabled){auto u=upsSnapshot();if(hid.SendReport(2,&u,sizeof(u),5))upsSent++;else upsSendFailed++;}
    }else hidNotReady++;
    drawScreen();displayDirty=false;
    updateLed(snapshot());
  }
  if(displayDirty){drawScreen();displayDirty=false;}
  streamHistory();
  delay(5);
}
