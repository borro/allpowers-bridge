#pragma once
#include <TelemetryState.h>
#include <stdio.h>
inline void currentPowerLabel(char *out,size_t capacity,const char *label,
                              const Telemetry &s,uint32_t now,bool output){
  const uint32_t field=output?AllpowersModels::OutputWatts:AllpowersModels::InputWatts;
  if(telemetryFresh(s,now)&&(s.known&field))
    snprintf(out,capacity,"%s %uW",label,output?s.outputWatts:s.chargeWatts);
  else snprintf(out,capacity,"%s --",label);
}
inline bool telemetryLabelsSelfTest(){
  Telemetry s;s.mainSeen=s.settingsSeen=true;s.source=PowerSource::Mains;
  s.mainAt=s.settingsAt=100;s.known=AllpowersModels::OutputWatts;s.outputWatts=123;
  char text[24];currentPowerLabel(text,sizeof(text),"OUT",s,101,true);
  if(strcmp(text,"OUT 123W"))return false;
  currentPowerLabel(text,sizeof(text),"OUT",s,15101,true);
  if(strcmp(text,"OUT --"))return false;
  currentPowerLabel(text,sizeof(text),"IN",s,101,false);
  return !strcmp(text,"IN --");
}
