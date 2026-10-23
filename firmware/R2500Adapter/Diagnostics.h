#pragma once
#include <stdio.h>
#include <string.h>
#include "Telemetry.h"
#include "StationIdentity.h"
#include "HistoryStore.h"

enum class DiagnosticTone:uint8_t {Plain,Dim,Good,Warning,Bad};
struct DiagnosticData {
  char version='?',phase[20]={};const char *reset="OTHER";
  uint32_t resetCode=0,nowMs=0,connects=0,drops=0;
  uint64_t uptimeSeconds=0;
  int scanRssi=-127;
  bool bleConnected=false,usbMounted=false,cdcOpen=false,upsEnabled=false;
  uint32_t upsGets=0,upsSent=0,upsFailed=0,heapFree=0,heapMin=0,psramFree=0;
  Telemetry telemetry;StationIdentity station;HistoryStats log;
};
struct DiagnosticRow {const char *label="";char value[18]={};DiagnosticTone tone=DiagnosticTone::Plain;};
struct DiagnosticView {
  static constexpr uint8_t Rows=13,Count=Rows*2;
  DiagnosticRow rows[Count];char heading[16]={},footer[32]={};
};
inline DiagnosticView diagnosticView(const DiagnosticData &d){
  DiagnosticView v;unsigned at=0;
  auto row=[&](const char *label,DiagnosticTone tone,const char *format,auto... args){
    auto &r=v.rows[at++];r.label=label;r.tone=tone;
    snprintf(r.value,sizeof(r.value),format,args...);
    for(char *p=r.value;*p;++p)if(uint8_t(*p)<32||uint8_t(*p)>126||*p=='"'||*p=='\\')*p='?';
    // Each column is 24 glyph cells. Preserve a full 17-character MAC while
    // keeping longer labels/values inside their own column.
    unsigned limit=23-strlen(label);if(limit<sizeof(r.value))r.value[limit]=0;
  };
  auto number=[&](const char *label,uint32_t n,DiagnosticTone tone=DiagnosticTone::Plain){row(label,tone,"%lu",(unsigned long)n);};
  auto age=[&](const char *label,bool seen,uint32_t stamp){
    uint32_t ms=d.nowMs-stamp;
    if(!seen)row(label,DiagnosticTone::Dim,"--");
    else row(label,ms<=15000?DiagnosticTone::Good:DiagnosticTone::Bad,"%lu.%luS",(unsigned long)(ms/1000),(unsigned long)((ms%1000)/100));
  };
  auto &s=d.telemetry;bool fresh=telemetryFresh(s,d.nowMs);
  snprintf(v.heading,sizeof(v.heading),"DIAG %c",d.version);
  snprintf(v.footer,sizeof(v.footer),"LOG %08lX CLK %s",(unsigned long)d.log.session,d.log.clockSynced?"YES":"NO");
  row("NAME",d.bleConnected?DiagnosticTone::Plain:DiagnosticTone::Dim,"%s",d.station.name[0]?d.station.name:"--");
  row("MAC",d.bleConnected?DiagnosticTone::Plain:DiagnosticTone::Dim,"%s",d.station.address[0]?d.station.address:"--");
  const char *profile=s.protocol==3?"R2500 V2":s.protocol==2?"LEGACY16":s.protocol==1?"OLD10":"UNKNOWN";
  row("PROFILE",DiagnosticTone::Plain,"%s",profile);
  row("BLE",d.bleConnected?DiagnosticTone::Good:DiagnosticTone::Warning,"%s",d.phase);
  if(d.scanRssi==-127)row("SCAN DBM",DiagnosticTone::Dim,"--");
  else row("SCAN DBM",DiagnosticTone::Plain,"%d",d.scanRssi);
  age("MAIN AGE",s.mainSeen,s.mainAt);age("CFG AGE",s.settingsSeen,s.settingsAt);
  number("CONNECT",d.connects);number("DROP",d.drops,d.drops?DiagnosticTone::Warning:DiagnosticTone::Plain);
  number("RX OK",s.validFrames);number("RX BAD",s.invalidFrames,s.invalidFrames?DiagnosticTone::Warning:DiagnosticTone::Good);
  if(s.settingsSeen&&(s.known&AllpowersModels::SourceRaw))row("SOURCE",fresh?DiagnosticTone::Plain:DiagnosticTone::Dim,"%04X %s",s.sourceRaw,s.source==PowerSource::Mains?"AC":s.source==PowerSource::Battery?"BAT":"?");
  else row("SOURCE",DiagnosticTone::Dim,"--");
  if(s.mainSeen)row("FLAGS",fresh?DiagnosticTone::Plain:DiagnosticTone::Dim,"M%02X S%02X",s.mainFlags,s.settingsFlags);
  else row("FLAGS",DiagnosticTone::Dim,"--");
  uint64_t sec=d.uptimeSeconds;
  row("UPTIME",DiagnosticTone::Plain,"%luD %02u:%02u:%02u",(unsigned long)(sec/86400),unsigned(sec/3600%24),unsigned(sec/60%60),unsigned(sec%60));
  row("RESET",DiagnosticTone::Plain,"%s %lu",d.reset,(unsigned long)d.resetCode);
  row("USB",d.usbMounted?DiagnosticTone::Good:DiagnosticTone::Dim,"%s",d.usbMounted?"HOST":"NO HOST");
  row("CDC",d.cdcOpen?DiagnosticTone::Good:DiagnosticTone::Dim,"%s",d.cdcOpen?"OPEN":"CLOSED");
  row("HID",d.upsEnabled?(fresh?DiagnosticTone::Good:DiagnosticTone::Warning):DiagnosticTone::Dim,"%s",d.upsEnabled?(fresh?"UPS LIVE":"UPS STALE"):"MAINT");
  number("HID GET",d.upsGets);number("HID TX",d.upsSent);
  number("TX ERR",d.upsFailed,d.upsFailed?DiagnosticTone::Bad:DiagnosticTone::Good);
  row("LOG",d.log.ready?DiagnosticTone::Good:DiagnosticTone::Bad,"%lu/%lu",(unsigned long)d.log.count,(unsigned long)HistoryFormat::kKeep);
  number("LOG ERR",d.log.errors,d.log.errors?DiagnosticTone::Bad:DiagnosticTone::Good);
  row("FLASH MS",DiagnosticTone::Plain,"%lu/%lu",(unsigned long)d.log.lastWriteMs,(unsigned long)d.log.maxWriteMs);
  row("HEAP K",DiagnosticTone::Plain,"%lu/%lu",(unsigned long)(d.heapFree/1024),(unsigned long)(d.heapMin/1024));
  row("PSRAM K",DiagnosticTone::Plain,"%lu",(unsigned long)(d.psramFree/1024));
  return v;
}
