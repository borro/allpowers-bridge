#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace BleDisconnects {
constexpr uint8_t kCapacity=8;
enum class Request:uint8_t {None,TelemetryTimeout,TargetChanged};
enum class Backend:uint8_t {Bluedroid,Nimble};
inline const char *requestName(Request request){
  switch(request){
    case Request::TelemetryTimeout:return "telemetry_timeout";
    case Request::TargetChanged:return "target_changed";
    default:return "none";
  }
}
// ESP-IDF esp_gatt_conn_reason_t values. Keep the full 16-bit raw value,
// including 0x100/0x101 and codes unknown to this version of the firmware.
inline const char *reasonName(uint16_t reason){
  switch(reason){
    case 0:return "UNKNOWN";
    case 1:return "L2CAP_FAILURE";
    case 0x08:return "CONNECTION_TIMEOUT";
    case 0x13:return "PEER_TERMINATED";
    case 0x16:return "LOCAL_HOST_TERMINATED";
    case 0x22:return "LMP_TIMEOUT";
    case 0x3e:return "FAILED_TO_ESTABLISH";
    case 0x100:return "CONNECTION_CANCELLED";
    case 0x101:return "NO_CONNECTION";
    default:return "UNRECOGNIZED";
  }
}
inline const char *reasonName(Backend backend,uint16_t reason){
  if(backend==Backend::Bluedroid)return reasonName(reason);
  // NimBLE wraps HCI status in BLE_HS_ERR_HCI_BASE (0x200). A host error
  // such as 0x16 NOT_SYNCED is not HCI 0x16 LOCAL_HOST_TERMINATED.
  if(reason>0x200&&reason<=0x2ff)return reasonName(uint16_t(reason-0x200));
  switch(reason){
    case 0:return "UNKNOWN";
    case 11:return "HOST_OS_ERROR";
    case 12:return "HOST_CONTROLLER_ERROR";
    case 19:return "HOST_HCI_TIMEOUT";
    case 22:return "HOST_NOT_SYNCED";
    default:return "HOST_ERROR";
  }
}
struct Context {
  uint64_t uptimeMs=0;
  uint32_t mainAgeMs=0,settingsAgeMs=0;
  bool mainSeen=false,settingsSeen=false;
  int8_t scanRssi=-127;
};
struct Event {
  Context context;
  uint32_t sequence=0,connection=0;
  uint16_t reason=0;
  bool reasonKnown=false;
  Request requested=Request::None;
  Backend backend=Backend::Bluedroid;
  uint8_t peer[6]{};
};
struct Snapshot {Event events[kCapacity]{};uint8_t count=0;};
// Caller holds dataLock. No allocation, I/O, flash writes or BLE API calls.
// Retain our binding because Arduino clears its own connection IDs before
// onDisconnect. Accept either raw-hook/callback order (NimBLE GAP listener or
// Bluedroid GATTC hook); never attribute another peer's event.
class Log {
  Event events[kCapacity]{};
  uint8_t next=0,count=0,gattInterfaceId=0,peer[6]{};
  uint16_t connectionId=0;
  uint32_t connection=0,sequence=0;
  bool active=false,pending=false;
  Request requested=Request::None;
  Backend backend=Backend::Bluedroid;
  uint16_t earlyReason=0;bool earlyReasonKnown=false;
public:
  void connected(uint8_t gattInterface,uint16_t id,const uint8_t *address,uint32_t token,Backend stack=Backend::Bluedroid){
    gattInterfaceId=gattInterface;connectionId=id;memcpy(peer,address,sizeof(peer));
    connection=token;active=true;pending=false;requested=Request::None;backend=stack;earlyReasonKnown=false;
  }
  void request(Request cause){if(active)requested=cause;}
  void requestFailed(uint32_t token){if(active&&connection==token)requested=Request::None;}
  void disconnected(const Context &context){
    if(!active)return;
    Event event;event.context=context;event.sequence=++sequence;event.connection=connection;
    event.requested=requested;event.backend=backend;event.reason=earlyReason;event.reasonKnown=earlyReasonKnown;
    memcpy(event.peer,peer,sizeof(peer));
    events[next]=event;next=uint8_t((next+1)%kCapacity);if(count<kCapacity)++count;
    active=false;pending=!earlyReasonKnown;
  }
  bool reason(uint8_t gattInterface,uint16_t id,const uint8_t *address,uint16_t code){
    if((!pending&&!active)||gattInterfaceId!=gattInterface||connectionId!=id||memcmp(peer,address,sizeof(peer)))return false;
    if(active){if(earlyReasonKnown)return false;earlyReason=code;earlyReasonKnown=true;return true;}
    auto &event=events[(next+kCapacity-1)%kCapacity];event.reason=code;event.reasonKnown=true;
    pending=false;return true;
  }
  Snapshot snapshot()const{
    Snapshot result;result.count=count;
    for(uint8_t i=0;i<count;++i)result.events[i]=events[(next+kCapacity-count+i)%kCapacity];
    return result;
  }
};
// Extra rows use the existing DIAG schema, so released desktop readers display
// them without changing the USB descriptor, STATUS handshake or history wire.
// Append only; return -1 on truncation and never hand partial JSON to USB.
inline int appendDiagnosticRows(char *out,size_t capacity,const Snapshot &snapshot){
  size_t used=0;
  for(uint8_t i=0;i<snapshot.count&&i<kCapacity;++i){
    const auto &e=snapshot.events[i];char mainAge[16]="unseen",settingsAge[16]="unseen",reason[12]="unavailable";
    if(e.context.mainSeen)snprintf(mainAge,sizeof(mainAge),"%lu",(unsigned long)e.context.mainAgeMs);
    if(e.context.settingsSeen)snprintf(settingsAge,sizeof(settingsAge),"%lu",(unsigned long)e.context.settingsAgeMs);
    if(e.reasonKnown)snprintf(reason,sizeof(reason),"0x%04X",unsigned(e.reason));
    const int n=snprintf(out+used,capacity-used,
      ",{\"label\":\"BLE DROP %lu\",\"value\":\"stack=%s up_ms=%llu conn=%lu peer=%02X:%02X:%02X:%02X:%02X:%02X reason=%s %s request=%s main_ms=%s cfg_ms=%s scan_dbm=%d\",\"tone\":3}",
      (unsigned long)e.sequence,e.backend==Backend::Nimble?"nimble":"bluedroid",(unsigned long long)e.context.uptimeMs,(unsigned long)e.connection,
      unsigned(e.peer[0]),unsigned(e.peer[1]),unsigned(e.peer[2]),unsigned(e.peer[3]),unsigned(e.peer[4]),unsigned(e.peer[5]),
      reason,e.reasonKnown?reasonName(e.backend,e.reason):"UNAVAILABLE",requestName(e.requested),mainAge,settingsAge,int(e.context.scanRssi));
    // Negative snprintf results convert above any valid buffer size on our
    // targets; the same comparison rejects errors and missing terminator space.
    if(size_t(n)>=capacity-used)return -1;
    used+=size_t(n);
  }
  return int(used);
}
}
