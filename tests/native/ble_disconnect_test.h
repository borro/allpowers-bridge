#pragma once
#include "BleDisconnectLog.h"
#include <assert.h>
#include <array>
#include <string>

namespace BleDisconnectTests {
inline void diagnosticReasons(){
  using namespace BleDisconnects;
  // These are the user-visible meanings of the SDK reason domains. Exercise
  // them through recording and DIAG output, including zero versus unavailable
  // and NimBLE host errors whose numbers overlap ordinary HCI reason codes.
  struct Reason {Backend backend;uint16_t raw;const char *name;};
  const Reason reasons[]={
    {Backend::Bluedroid,0,"UNKNOWN"},
    {Backend::Bluedroid,1,"L2CAP_FAILURE"},
    {Backend::Bluedroid,0x08,"CONNECTION_TIMEOUT"},
    {Backend::Bluedroid,0x13,"PEER_TERMINATED"},
    {Backend::Bluedroid,0x16,"LOCAL_HOST_TERMINATED"},
    {Backend::Bluedroid,0x22,"LMP_TIMEOUT"},
    {Backend::Bluedroid,0x3e,"FAILED_TO_ESTABLISH"},
    {Backend::Bluedroid,0x100,"CONNECTION_CANCELLED"},
    {Backend::Bluedroid,0x101,"NO_CONNECTION"},
    {Backend::Bluedroid,0xffff,"UNRECOGNIZED"},
    {Backend::Nimble,0x208,"CONNECTION_TIMEOUT"},
    {Backend::Nimble,0x213,"PEER_TERMINATED"},
    {Backend::Nimble,0x216,"LOCAL_HOST_TERMINATED"},
    {Backend::Nimble,0x222,"LMP_TIMEOUT"},
    {Backend::Nimble,0x23e,"FAILED_TO_ESTABLISH"},
    {Backend::Nimble,0x2ff,"UNRECOGNIZED"},
    {Backend::Nimble,0,"UNKNOWN"},
    {Backend::Nimble,11,"HOST_OS_ERROR"},
    {Backend::Nimble,12,"HOST_CONTROLLER_ERROR"},
    {Backend::Nimble,19,"HOST_HCI_TIMEOUT"},
    {Backend::Nimble,22,"HOST_NOT_SYNCED"},
    {Backend::Nimble,1,"HOST_ERROR"},
    {Backend::Nimble,0x200,"HOST_ERROR"},
    {Backend::Nimble,0x300,"HOST_ERROR"},
    {Backend::Nimble,0xffff,"HOST_ERROR"},
  };
  struct Intent {Request value;const char *name;};
  const Intent intents[]={{Request::None,"none"},{Request::TelemetryTimeout,"telemetry_timeout"},
                          {Request::TargetChanged,"target_changed"}};
  const uint8_t peer[6]={2,0,0,0,0,1};
  for(const auto &reason:reasons)for(const auto &intent:intents)for(bool rawFirst:{false,true}){
    Log log;log.connected(1,2,peer,7,reason.backend);log.request(intent.value);
    if(rawFirst)assert(log.reason(1,2,peer,reason.raw));
    log.disconnected({12345,20,100,true,true,-89});
    if(!rawFirst)assert(log.reason(1,2,peer,reason.raw));
    // Reconnection must preserve the previous event, reason and request even
    // after the active binding has moved to another connection.
    log.connected(1,3,peer,8,reason.backend);
    const auto snapshot=log.snapshot();assert(snapshot.count==1);
    assert(snapshot.events[0].reasonKnown&&snapshot.events[0].reason==reason.raw);
    assert(snapshot.events[0].requested==intent.value&&snapshot.events[0].connection==7);
    char output[1024],expected[160];
    const int size=appendDiagnosticRows(output,sizeof(output),snapshot);
    assert(size>0&&size<int(sizeof(output)));
    snprintf(expected,sizeof(expected),"reason=0x%04X %s request=%s",unsigned(reason.raw),reason.name,intent.name);
    assert(strstr(output,expected));
    assert(strstr(output,reason.backend==Backend::Nimble?"stack=nimble":"stack=bluedroid"));
    assert(strstr(output,"up_ms=12345 conn=7 peer=02:00:00:00:00:01"));
    assert(strstr(output,"main_ms=20 cfg_ms=100 scan_dbm=-89"));
    assert(!strstr(output,"unavailable"));
  }
}
inline void run(){
  diagnosticReasons();
  using namespace BleDisconnects;
  const uint8_t peer[6]={2,0,0,0,0,1},other[6]={2,0,0,0,0,2};
  Log log;
  assert(log.snapshot().count==0);
  assert(!log.reason(1,2,peer,8));
  // Actual Arduino ordering: high-level callback captures before its state is
  // cleared; the later raw hook completes that record with its original key.
  log.connected(1,2,peer,7);
  log.disconnected({UINT64_C(4294967300),20,100,true,true,-89});
  auto pending=log.snapshot();
  assert(pending.count==1&&!pending.events[0].reasonKnown);
  assert(!log.reason(2,2,peer,0x13));
  assert(!log.reason(1,3,peer,0x13));
  assert(!log.reason(1,2,other,0x13));
  assert(log.reason(1,2,peer,8));
  assert(!log.reason(1,2,peer,0x16));
  log.disconnected({});
  auto s=log.snapshot();
  assert(s.count==1&&s.events[0].reason==8&&s.events[0].reasonKnown);
  assert(s.events[0].context.uptimeMs==UINT64_C(4294967300));
  assert(s.events[0].context.mainAgeMs==20&&s.events[0].context.settingsAgeMs==100);
  assert(s.events[0].context.mainSeen&&s.events[0].context.settingsSeen);
  assert(s.events[0].context.scanRssi==-89&&s.events[0].requested==Request::None);
  assert(s.events[0].connection==7&&!memcmp(s.events[0].peer,peer,6));
  for(auto request:{Request::TelemetryTimeout,Request::TargetChanged}){
    log.connected(1,2,peer,8);
    log.request(request);log.disconnected({});
    assert(log.reason(1,2,peer,0x16));
    s=log.snapshot();assert(s.events[s.count-1].requested==request);
  }
  log.connected(1,2,peer,9);log.request(Request::TelemetryTimeout);
  log.requestFailed(8);
  log.disconnected({});assert(log.reason(1,2,peer,0x16));
  s=log.snapshot();assert(s.events[s.count-1].requested==Request::TelemetryTimeout);
  log.connected(1,2,peer,10);log.request(Request::TargetChanged);log.requestFailed(10);
  log.disconnected({});assert(log.reason(1,2,peer,0x13));
  s=log.snapshot();assert(s.events[s.count-1].requested==Request::None);
  // A missing raw callback stays unavailable after reconnect; never fabricate
  // code zero or annotate a new connection with an old peer's event.
  log.connected(1,2,peer,11);log.disconnected({});
  log.connected(1,3,other,12);
  assert(!log.reason(1,2,peer,8));
  log.disconnected({});assert(log.reason(1,3,other,0x100));
  s=log.snapshot();assert(!s.events[s.count-2].reasonKnown);
  assert(s.events[s.count-1].reason==0x100);
  assert(!strcmp(reasonName(0x100),"CONNECTION_CANCELLED"));
  assert(!strcmp(reasonName(0xffff),"UNRECOGNIZED"));
  Log nimble;nimble.connected(0,2,peer,1,Backend::Nimble);
  assert(nimble.reason(0,2,peer,0x208)); // Listener may precede onDisconnect.
  assert(!nimble.reason(0,2,peer,0x213));
  nimble.disconnected({});
  auto ns=nimble.snapshot();assert(ns.count==1&&ns.events[0].reasonKnown&&ns.events[0].reason==0x208);
  assert(ns.events[0].backend==Backend::Nimble);
  assert(!strcmp(reasonName(Backend::Nimble,0x208),"CONNECTION_TIMEOUT"));
  assert(!strcmp(reasonName(Backend::Nimble,22),"HOST_NOT_SYNCED"));
  assert(!strcmp(reasonName(Backend::Nimble,12),"HOST_CONTROLLER_ERROR"));
  for(uint32_t token=13;token<30;++token){
    log.connected(1,2,peer,token);log.disconnected({});assert(log.reason(1,2,peer,0x101));
  }
  s=log.snapshot();assert(s.count==kCapacity);
  for(uint8_t i=0;i<kCapacity;++i)assert(s.events[i].connection==22u+i);
  // Worst-width values fit the mailbox and desktop's 256-character values.
  for(auto &event:s.events){
    event.sequence=UINT32_MAX;event.connection=UINT32_MAX;
    event.context={UINT64_MAX,UINT32_MAX,UINT32_MAX,true,true,-128};
    event.reason=0x16;event.requested=Request::TelemetryTimeout;
  }
  std::array<char,4096> output{};
  const int length=appendDiagnosticRows(output.data(),output.size(),s);
  assert(length>0&&length<3500);
  std::string json(output.data(),size_t(length));
  size_t at=0,rows=0;
  while((at=json.find("\"value\":\"",at))!=std::string::npos){
    at+=9;const auto end=json.find('"',at);assert(end!=std::string::npos&&end-at<=256);at=end;++rows;
  }
  assert(rows==kCapacity);
  std::array<char,4096> bounded;bounded.fill('#');
  assert(appendDiagnosticRows(bounded.data(),size_t(length),s)==-1);
  assert(bounded[size_t(length)]=='#');
  assert(appendDiagnosticRows(bounded.data(),size_t(length)+1,s)==length);
  assert(!memcmp(bounded.data(),output.data(),size_t(length)+1));
  assert(appendDiagnosticRows(bounded.data(),0,s)==-1);
  Snapshot empty;assert(appendDiagnosticRows(bounded.data(),0,empty)==0);
  s.count=1;s.events[0].reasonKnown=false;s.events[0].context.mainSeen=s.events[0].context.settingsSeen=false;
  assert(appendDiagnosticRows(output.data(),output.size(),s)>0);
  assert(strstr(output.data(),"reason=unavailable UNAVAILABLE"));
  assert(strstr(output.data(),"main_ms=unseen cfg_ms=unseen"));
}
}
