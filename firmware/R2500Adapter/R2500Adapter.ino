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
#include <TelemetryRecovery.h>

#include "BuildVersion.h"
#include "ProductIdentity.h"
#include "HistoryCompatibility.h"
#include "HidTelemetryServer.h"
#include "StatusWire.h"
#include <freertos/queue.h>
#if ARDUINO_USB_MODE != 0 || ARDUINO_USB_CDC_ON_BOOT
#error Use TinyUSB OTG without CDC; register all HID collections before USB starts.
#endif
static portMUX_TYPE dataLock = portMUX_INITIALIZER_UNLOCKED;
static Telemetry telemetry;
static UpsReport lastValidUps={};
static StationIdentity stationIdentity;
static bool bleReady=false, connected=false;
static int stationRssi=-127;
static uint32_t scanRounds=0, advertisements=0;
static std::atomic<bool> requestBoot{false}, requestRestart{false};
static std::atomic<bool> usbStarted{false};
static bool upsEnabled=false, displayReady=false, parserPassed=false, hidParserPassed=false;
static bool bleTaskStarted=false,historyTaskStarted=false;
static std::atomic<bool> bleStartupFailed{false};
static std::atomic<uint32_t> upsGets{0}, staticGets{0};
static uint32_t upsSent=0,upsSendFailed=0,vendorSent=0,vendorSendFailed=0,hidNotReady=0;
static uint32_t bleConnects=0,bleDrops=0;
static BleDisconnects::Log bleDisconnectLog;
static USBHID hid;
static Dashboard screen;
static HistoryStore history;
static portMUX_TYPE hidLock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t hidRequests=nullptr;
static uint64_t hidBoot=0;
static uint8_t hidLive[HidTelemetry::Payload]{};
static char pendingNotification[600]{};
static size_t pendingNotificationBytes=0;
static bool captureStatus(HistoryFormat::StatusSnapshot &frame,uint32_t now);
class FrozenHistorySource:public HistoryFormat::ExportSource {
  HistoryFormat::Record *memory=nullptr;HistoryFormat::FrozenArchive *archive=nullptr;
public:
  HistoryFormat::ReadState open(HistoryFormat::ExportMetadata &metadata)override{
    if(!memory)memory=static_cast<HistoryFormat::Record*>(ps_malloc(HistoryFormat::kKeep*sizeof(*memory)));
    if(!memory)return HistoryFormat::ReadState::Error;
    if(!archive)archive=new(std::nothrow) HistoryFormat::FrozenArchive(memory,HistoryFormat::kKeep);
    return archive?history.capture(*archive,metadata):HistoryFormat::ReadState::Error;
  }
  HistoryFormat::ReadState read(uint32_t ordinal,HistoryFormat::Record &record)override{
    return archive?archive->read(ordinal,record):HistoryFormat::ReadState::Error;
  }
  void close()override{delete archive;archive=nullptr;free(memory);memory=nullptr;}
} hidArchives[2];
static size_t diagnosticText(uint8_t *out,size_t capacity);
class HidProvider:public HidTelemetry::Provider {
public:
  size_t text(HidTelemetry::Op op,uint8_t *out,size_t capacity)override{
    if(op==HidTelemetry::Op::Diagnostics)return diagnosticText(out,capacity);
    HistoryFormat::StatusSnapshot frame;if(!captureStatus(frame,millis()))return 0;
    size_t used=0;
    for(uint8_t i=0;i<HistoryFormat::kStatusRows;++i){
      int n=HistoryFormat::serializeStatus(frame,i,reinterpret_cast<char*>(out+used),capacity-used);
      if(n<=0||size_t(n)>=capacity-used)return 0;used+=size_t(n);
    }
    return used;
  }
  HistoryFormat::ReadState open(unsigned slot,HistoryFormat::ExportMetadata &meta,uint32_t &digest)override{
    auto state=hidArchives[slot].open(meta);
    if(state!=HistoryFormat::ReadState::Record){hidArchives[slot].close();return state;}
    digest=HistoryFormat::kDigestSeed;
    for(uint32_t i=0;i<meta.count;++i){HistoryFormat::Record r;
      if(hidArchives[slot].read(i,r)!=HistoryFormat::ReadState::Record){hidArchives[slot].close();return HistoryFormat::ReadState::Error;}
      digest=HistoryFormat::digestRecord(digest,r);
    }
    return state;
  }
  bool record(unsigned slot,uint32_t ordinal,HistoryFormat::Record &out)override{
    return hidArchives[slot].read(ordinal,out)==HistoryFormat::ReadState::Record;
  }
  void close(unsigned slot)override{hidArchives[slot].close();}
  HidTelemetry::Result clock(uint32_t epoch,uint32_t uptime)override{
    Telemetry copy;portENTER_CRITICAL(&dataLock);copy=telemetry;portEXIT_CRITICAL(&dataLock);
    return history.setTime(epoch,uptime,copy)?HidTelemetry::Result::Ok:HidTelemetry::Result::Error;
  }
} hidProvider;
static HidTelemetry::Server hidServer(hidProvider);
static BootGestures button;
static uint8_t displayPage=0;
static bool displayDirty=false;
static uint32_t buttonPresses=0;
static bool buttonPassed=false;
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
using ConnectionView=HistoryFormat::StatusConnection;
static ConnectionView connectionSnapshot(){
  portENTER_CRITICAL(&dataLock);
  ConnectionView copy{telemetry,stationIdentity,connected,pickerBack,scanning,targetSaved,noUpsProfile,connectionState};
  portEXIT_CRITICAL(&dataLock);return copy;
}
static StationPicker::Snapshot menuSnapshot(){
  portENTER_CRITICAL(&dataLock);auto first=deviceCatalog.snapshot();
  uint8_t selected=pickerBack?first.total:first.selectedIndex<0?0:uint8_t(first.selectedIndex);
  auto page=deviceCatalog.snapshot((selected/3)*3);portEXIT_CRITICAL(&dataLock);return page;
}
static void drawScreen(){
  auto state=connectionSnapshot();
  if(pickerOpen)screen.renderPicker(menuSnapshot(),state.pickerBack,state.scanning,state.phase,state.connected?state.identity:StationIdentity{});
  else if(displayPage==1)screen.renderHistory(history,state.telemetry);
  else if(displayPage>=2)screen.renderDiagnostics(diagnosticsSnapshot(),displayPage-2);
  else screen.render(state.telemetry,state.identity,state.connected,millis());
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
static bool telemetryDescriptorSelfTest(){
  auto *map=esp_hid_parse_report_map(HidTelemetry::Descriptor,sizeof(HidTelemetry::Descriptor));
  if(!map)return false;
  unsigned seen=0;
  for(uint8_t i=0;i<map->reports_len;++i){const auto &r=map->reports[i];
    if(r.protocol_mode!=ESP_HID_PROTOCOL_MODE_REPORT||r.value_len!=HidTelemetry::Payload)continue;
    if(r.report_id==4&&r.report_type==ESP_HID_REPORT_TYPE_FEATURE)seen|=1;
    if(r.report_id==5&&r.report_type==ESP_HID_REPORT_TYPE_FEATURE)seen|=2;
    if(r.report_id==6&&r.report_type==ESP_HID_REPORT_TYPE_OUTPUT)seen|=4;
    if(r.report_id==7&&r.report_type==ESP_HID_REPORT_TYPE_INPUT)seen|=8;
  }
  esp_hid_free_report_map(map);return seen==15;
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
class TelemetryHID:public USBHIDDevice {
public:
  bool enable(){return USBHID::addDevice(this,sizeof(HidTelemetry::Descriptor));}
  uint16_t _onGetDescriptor(uint8_t *b)override{
    memcpy(b,HidTelemetry::Descriptor,sizeof(HidTelemetry::Descriptor));return sizeof(HidTelemetry::Descriptor);
  }
  uint16_t _onGetFeature(uint8_t id,uint8_t *b,uint16_t n)override{
    if(n<HidTelemetry::Payload)return 0;
    if(id==HidTelemetry::CapsReport)HidTelemetry::capabilities(b,hidBoot,true);
    else if(id==HidTelemetry::LiveReport){
      portENTER_CRITICAL(&hidLock);memcpy(b,hidLive,sizeof(hidLive));portEXIT_CRITICAL(&hidLock);
    }else return 0;
    return HidTelemetry::Payload;
  }
  void _onOutput(uint8_t id,const uint8_t *b,uint16_t n)override{
    HidTelemetry::Request r;
    if(id==HidTelemetry::RequestReport&&hidRequests&&HidTelemetry::decode(b,n,r)&&r.boot==hidBoot)
      xQueueSend(hidRequests,&r,0);
  }
} telemetryHid;
static void refreshHidLive(){
  using namespace HidTelemetry;
  const auto view=connectionSnapshot();const auto &s=view.telemetry;
  Live v;v.boot=hidBoot;v.uptime=millis();v.model=s.protocol;v.known=s.known;
  v.uptimeSeconds=uint32_t(uint64_t(esp_timer_get_time())/1000000);
  v.flags=(view.connected?Connected:0)|(telemetryFresh(s,v.uptime)?Fresh:0)|
    (s.mainSeen?MainSeen:0)|(s.settingsSeen?SettingsSeen:0)|(s.mainFlags&2?AcOn:0)|
    (s.mainFlags&1?UsbOn:0)|(s.settingsFlags&0x10?DcOn:0)|(upsEnabled?UpsEnabled:0);
  unsigned mac[6]{};
  if(sscanf(view.identity.address,"%2x:%2x:%2x:%2x:%2x:%2x",&mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5])==6)
    for(unsigned i=0;i<6;++i)v.address[i]=uint8_t(mac[i]);
  v.soc=s.soc;v.input=s.chargeWatts;v.output=s.outputWatts;v.runtime=uint32_t(s.minutes)*60;
  v.source=uint8_t(s.source);v.sourceRaw=s.sourceRaw;
  v.mainAge=s.mainSeen?uint32_t(v.uptime-s.mainAt):UINT32_MAX;
  v.settingsAge=s.settingsSeen?uint32_t(v.uptime-s.settingsAt):UINT32_MAX;
  v.valid=s.validFrames;v.invalid=s.invalidFrames;v.ups=upsSnapshot().status;v.firmware=ADAPTER_VERSION;
  uint8_t bytes[Payload];encode(v,bytes);
  portENTER_CRITICAL(&hidLock);memcpy(hidLive,bytes,sizeof(bytes));portEXIT_CRITICAL(&hidLock);
}
static void notifyStation(BLERemoteCharacteristic *sender,uint8_t *bytes,size_t length,bool,uint32_t revision,uint32_t token) {
  portENTER_CRITICAL(&dataLock);
  if(revision!=activeRevision||revision!=targetRevision||token!=activeConnectionToken||sender!=activeRx){portEXIT_CRITICAL(&dataLock);return;}
  bool valid=decodeStationTelemetry(telemetry,bytes,length,millis(),activeModel);
  portEXIT_CRITICAL(&dataLock);
  if(usbStarted && length<=256){
    char hex[513];for(size_t i=0;i<length;++i)snprintf(hex+i*2,3,"%02x",bytes[i]);hex[length*2]=0;
    portENTER_CRITICAL(&hidLock);
    if(!pendingNotificationBytes){
      const int n=snprintf(pendingNotification,sizeof(pendingNotification),"{\"station_notification_hex\":\"%s\",\"valid\":%s}\n",hex,valid?"true":"false");
      if(n>0&&size_t(n)<sizeof(pendingNotification))pendingNotificationBytes=size_t(n);
    }
    portEXIT_CRITICAL(&hidLock);
  }
}
class ClientEvents:public BLEClientCallbacks {
  void onConnect(BLEClient *client)override{
    auto peer=client->getPeerAddress();
#if defined(CONFIG_NIMBLE_ENABLED)
    uint8_t address[6];for(unsigned i=0;i<6;++i)address[i]=peer.getNative()[5-i];
    const uint8_t gattInterface=0;const auto backend=BleDisconnects::Backend::Nimble;
#else
    const uint8_t *address=peer.getNative();
    const uint8_t gattInterface=client->getGattcIf();const auto backend=BleDisconnects::Backend::Bluedroid;
#endif
    portENTER_CRITICAL(&dataLock);connected=true;bleConnects++;
    bleDisconnectLog.connected(gattInterface,client->getConnId(),address,activeConnectionToken,backend);
    portEXIT_CRITICAL(&dataLock);
  }
  void onDisconnect(BLEClient*)override{
    const uint32_t now=millis();const uint64_t uptimeMs=uint64_t(esp_timer_get_time())/1000;
    portENTER_CRITICAL(&dataLock);
    bleDisconnectLog.disconnected({uptimeMs,now-telemetry.mainAt,now-telemetry.settingsAt,telemetry.mainSeen,telemetry.settingsSeen,int8_t(stationRssi)});
    connected=false;bleDrops++;activeRx=nullptr;telemetry.mainSeen=false;telemetry.settingsSeen=false;connectionState="DISCONNECTED";portEXIT_CRITICAL(&dataLock);
  }
} clientEvents;
class DisconnectReasonEvents {
public:
#if defined(CONFIG_NIMBLE_ENABLED)
static int receive(ble_gap_event *event,void*){
  if(!event||event->type!=BLE_GAP_EVENT_DISCONNECT)return 0;
  uint8_t address[6];for(unsigned i=0;i<6;++i)address[i]=event->disconnect.conn.peer_ota_addr.val[5-i];
  portENTER_CRITICAL(&dataLock);
  bleDisconnectLog.reason(0,event->disconnect.conn.conn_handle,address,uint16_t(event->disconnect.reason));
  portEXIT_CRITICAL(&dataLock);return 0;
}
#else
static void receive(esp_gattc_cb_event_t event,esp_gatt_if_t gattInterface,esp_ble_gattc_cb_param_t *param){
  if(event!=ESP_GATTC_DISCONNECT_EVT||!param)return;
  portENTER_CRITICAL(&dataLock);
  bleDisconnectLog.reason(gattInterface,param->disconnect.conn_id,param->disconnect.remote_bda,uint16_t(param->disconnect.reason));
  portEXIT_CRITICAL(&dataLock);
}
#endif
};
static void disconnectStation(BLEClient *client,BleDisconnects::Request cause,uint32_t token){
  portENTER_CRITICAL(&dataLock);bleDisconnectLog.request(cause);portEXIT_CRITICAL(&dataLock);
  // Preserve the original call and policy. A rejected close is not a local
  // disconnect request for an unrelated future radio/peer failure.
  if(client->disconnect()!=ESP_OK){
    portENTER_CRITICAL(&dataLock);bleDisconnectLog.requestFailed(token);portEXIT_CRITICAL(&dataLock);
  }
}
static void statusRequest(BLERemoteCharacteristic *tx) {
  if(!tx || !(tx->canWrite()||tx->canWriteNoResponse()))return;
  portENTER_CRITICAL(&dataLock);auto model=activeModel;portEXIT_CRITICAL(&dataLock);
  auto request=AllpowersModels::readOnlyPoll(model);
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
      portENTER_CRITICAL(&dataLock);deviceCatalog.upsert(e,now,connected?stationIdentity.address:nullptr,requestedTarget.identity.address);portEXIT_CRITICAL(&dataLock);
    }
  }
  scan->clearResults();
  portENTER_CRITICAL(&dataLock);deviceCatalog.prune(now,connected?stationIdentity.address:nullptr);scanning=false;portEXIT_CRITICAL(&dataLock);
}
static void bleTask(void*) {
  if(!BLEDevice::init("R2500 USB UPS")){bleStartupFailed=true;vTaskDelete(nullptr);return;}
#if defined(CONFIG_NIMBLE_ENABLED)
  BLEDevice::setCustomGapHandler(DisconnectReasonEvents::receive);
#else
  BLEDevice::setCustomGattcHandler(DisconnectReasonEvents::receive);
#endif
  portENTER_CRITICAL(&dataLock);bleReady=true;portEXIT_CRITICAL(&dataLock);
  BLEScan *scan=BLEDevice::getScan();BLEClient *client=BLEDevice::createClient();
  if(!scan||!client){bleStartupFailed=true;vTaskDelete(nullptr);return;}
  scan->setActiveScan(true);scan->setInterval(160);scan->setWindow(80);client->setClientCallbacks(&clientEvents);
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
      noUpsProfile=activeModel!=AllpowersModels::Model::R2500V2;connectionState="CONNECTING";
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
        AllpowersCore::TelemetryRecovery recovery;recovery.begin(since);
        while(client->isConnected()){
          vTaskDelay(pdMS_TO_TICKS(100));uint32_t now=millis(),current;
          portENTER_CRITICAL(&dataLock);current=targetRevision;
          bool complete=telemetryFresh(telemetry,now);
          bool rescan=scanRequested||(pickerOpen&&uint32_t(now-lastScan)>=8000);portEXIT_CRITICAL(&dataLock);
          if(current!=revision){disconnectStation(client,BleDisconnects::Request::TargetChanged,token);break;}
          if(requestBoot||requestRestart)continue;
          if(rescan){scanNearby(scan,2);lastScan=millis();}
          if(subscribed&&uint32_t(now-keepalive)>=AllpowersCore::kPollIntervalMs){statusRequest(tx);keepalive=now;}
          recovery.observe(now,complete);
          // Main-only/settings-only traffic and a missing Notify RX must not
          // keep an unusable connection alive forever. Poll first, reconnect
          // after the bounded grace period even when no first snapshot arrived.
          if(recovery.reconnectDue(now)){disconnectStation(client,BleDisconnects::Request::TelemetryTimeout,token);break;}
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
  d.bleDisconnects=bleDisconnectLog.snapshot();
  portEXIT_CRITICAL(&dataLock);
  d.nowMs=millis();d.uptimeSeconds=uint64_t(esp_timer_get_time())/1000000;
  d.log=history.status();d.reset=resetReasonName();d.resetCode=uint32_t(esp_reset_reason());
  d.usbMounted=bool(USB);d.cdcOpen=false;d.upsEnabled=upsEnabled;
  d.upsGets=upsGets;d.upsSent=upsSent;d.upsFailed=upsSendFailed;
  d.heapFree=ESP.getFreeHeap();d.heapMin=ESP.getMinFreeHeap();d.psramFree=ESP.getFreePsram();return d;
}
static size_t diagnosticText(uint8_t *out,size_t capacity){
  auto d=diagnosticsSnapshot();auto view=diagnosticView(d);static char body[6144];
  size_t used=snprintf(body,sizeof(body),"{\"diagnostic\":true,\"version\":\"%c\",\"page\":%u,\"heading\":\"%s\",\"footer\":\"%s\",\"rows\":[",ADAPTER_VERSION,displayPage,view.heading,view.footer);
  for(unsigned i=0;i<DiagnosticView::Count&&used<sizeof(body)-3;++i){auto &r=view.rows[i];
    used+=snprintf(body+used,sizeof(body)-used,"%s{\"label\":\"%s\",\"value\":\"%s\",\"tone\":%u}",i?",":"",r.label,r.value,unsigned(r.tone));
  }
  if(used>=sizeof(body)-3)return 0;
  const int extra=BleDisconnects::appendDiagnosticRows(body+used,sizeof(body)-used-3,d.bleDisconnects);
  if(extra<0)return 0;
  used+=size_t(extra);memcpy(body+used,"]}\n",3);if(used+3>capacity)return 0;memcpy(out,body,used+3);return used+3;
}
static bool captureStatus(HistoryFormat::StatusSnapshot &frame,uint32_t now){
  frame={};frame.nowMs=now;frame.state=connectionSnapshot();auto report=upsSnapshot();
  frame.ups.runtime_seconds=report.runtime_seconds;frame.ups.status=report.status;
  frame.historyAvailable=history.tryStatus(frame.log);uint32_t revision;frame.target=targetSnapshot(revision);
  portENTER_CRITICAL(&dataLock);frame.nearby=deviceCatalog.snapshot();portEXIT_CRITICAL(&dataLock);
  auto d=diagnosticsSnapshot();frame.d={d.usbMounted,d.connects,d.drops,d.heapFree,d.heapMin,d.psramFree,d.resetCode};
  frame.productVersion=AP_PRODUCT_VERSION;frame.productCommit=AP_PRODUCT_COMMIT;frame.productDirty=AP_PRODUCT_DIRTY;
  frame.protocolVersion=ADAPTER_VERSION;frame.boardId=Board::id;frame.panelName=Board::panelName;
  frame.panelWidth=Board::width;frame.panelHeight=Board::height;frame.rgbAvailable=Board::rgb>=0;frame.screenCount=Board::screenCount;
  frame.upsEnabled=upsEnabled;
  frame.parserPassed=parserPassed;
  frame.displayReady=displayReady;
  frame.bleTaskStarted=bleTaskStarted;
  frame.historyTaskStarted=historyTaskStarted;
  frame.bleStartupFailed=bleStartupFailed;
  frame.pickerPassed=pickerPassed;
  frame.pickerOpen=pickerOpen;
  frame.targetLoaded=targetLoaded;
  frame.buttonPassed=buttonPassed;
  frame.upsSent=upsSent;
  frame.upsSendFailed=upsSendFailed;
  frame.vendorSent=vendorSent;
  frame.vendorSendFailed=vendorSendFailed;
  frame.hidNotReady=hidNotReady;
  frame.upsGets=upsGets;
  frame.staticGets=staticGets;
  frame.buttonPresses=buttonPresses;
  frame.displayPage=displayPage;
  return true;
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
static void pumpHid(){
  using namespace HidTelemetry;
  static Request current;static uint8_t message[MessageLimit];
  static size_t size=0,offset=0;static Result result=Result::Ok;static uint32_t started=0;
  const uint32_t now=millis();hidServer.expire(now);
  if(size&&uint32_t(now-started)>5000){size=offset=0;}
  if(!size){
    if(hidRequests&&xQueueReceive(hidRequests,&current,0)==pdTRUE){
      result=hidServer.respond(current,now,message,size);offset=0;started=now;
    }else{
      portENTER_CRITICAL(&hidLock);
      if(pendingNotificationBytes){
        memcpy(message,pendingNotification,pendingNotificationBytes);size=pendingNotificationBytes;
        pendingNotificationBytes=0;current={};current.op=Op::Packet;current.boot=hidBoot;
        offset=0;started=now;result=Result::Ok;
      }
      portEXIT_CRITICAL(&hidLock);
    }
  }
  if(size&&hid.ready()){
    uint8_t report[Payload];size_t count=fragment(current,result,message,size,offset,report);
    if(count&&hid.SendReport(ResponseReport,report,sizeof(report),5)){
      offset+=count;if(offset==size)size=offset=0;
    }
  }
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
  if(telemetryDescriptorSelfTest())telemetryHid.enable();
  USB.usbClass(0);USB.usbSubClass(0);USB.usbProtocol(0);refreshHidLive();
  hid.begin();USB.begin();usbStarted=true;
}
void setup() {
  hidBoot=(uint64_t(esp_random())<<32)|esp_random();if(!hidBoot)hidBoot=1;
  hidRequests=xQueueCreate(8,sizeof(HidTelemetry::Request));
  buttonPassed=BootGestures::selfTest();pickerPassed=StationPicker::selfTest()&&AllpowersModels::selfTest();
  loadLastTarget();pinMode(Board::boot,INPUT_PULLUP);button.begin(digitalRead(Board::boot)==LOW,millis());
  led(0,0,12); // Optional board-specific RGB; absent on Pocket.
  parserPassed=telemetrySelfTest()&&pickerPassed;displayReady=screen.begin();
  if(parserPassed)bleTaskStarted=xTaskCreatePinnedToCore(bleTask,"station_ble",10240,nullptr,1,nullptr,0)==pdPASS;
  uint32_t start=millis();
  // Never enumerate a battery with fabricated zero SOC before station data arrives.
  // A bounded timeout always exposes maintenance vendor HID for recovery.
  while(targetLoaded&&parserPassed&&bleTaskStarted&&!bleStartupFailed&&!upsTelemetryFresh(snapshot(),millis())&&!(connectionSnapshot().connected&&connectionSnapshot().noUpsProfile)&&uint32_t(millis()-start)<25000){
    updateLed(snapshot());drawScreen();delay(200);
  }
  startUsb(parserPassed&&upsTelemetryFresh(snapshot(),millis()));
  if(history.begin())historyTaskStarted=xTaskCreatePinnedToCore(historyTask,"history_log",6144,nullptr,1,nullptr,1)==pdPASS;
}
void loop() {
  static uint32_t lastReport=0,bootDeadline=0;
  if(buttonPassed)menuGesture(button.update(digitalRead(Board::boot)==LOW,millis()));
  if(requestBoot||requestRestart){
    hidServer.stop();history.requestPause();
    if(!bootDeadline){updateLed(snapshot());bootDeadline=millis()+150;}
    if(int32_t(millis()-bootDeadline)>=0&&history.pausedAndIdle()&&settingsIdle()){
      if(requestBoot)usb_persist_restart(RESTART_BOOTLOADER);else ESP.restart();
    }
  }
  auto state=connectionSnapshot();
  if(state.targetSaved&&!pickerOpen&&state.connected&&digitalRead(Board::boot)==HIGH){
    if((upsEnabled&&state.noUpsProfile)||(!upsEnabled&&upsTelemetryFresh(snapshot(),millis())))requestRestart=true;
  }
  uint32_t now=millis();
  if(uint32_t(now-lastReport)>=1000){
    lastReport=now;refreshHidLive();auto p=probeSnapshot();
    if(hid.ready()){
      if(hid.SendReport(1,&p,sizeof(p),5))vendorSent++;else vendorSendFailed++;
      if(upsEnabled){auto u=upsSnapshot();if(hid.SendReport(2,&u,sizeof(u),5))upsSent++;else upsSendFailed++;}
    }else hidNotReady++;
    drawScreen();displayDirty=false;
    updateLed(snapshot());
  }
  if(displayDirty){drawScreen();displayDirty=false;}
  if(!requestBoot&&!requestRestart)pumpHid();
  delay(5);
}
