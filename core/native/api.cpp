#include "api.h"
#include "AllpowersCore.h"
#include <stdlib.h>
#include <new>
using AllpowersCore::Session;
static AllpowersModels::Model model(uint32_t m){return m<=3?AllpowersModels::Model(m):AllpowersModels::Model::Unknown;}
uint32_t ap_abi_version(){return AP_ABI_VERSION;}
void *ap_create(uint32_t m){void *p=malloc(sizeof(Session));return p?new(p) Session(model(m)):nullptr;}
void ap_destroy(void *p){if(p){static_cast<Session*>(p)->~Session();free(p);}}
void ap_reset(void *p,uint32_t m){if(p)static_cast<Session*>(p)->reset(model(m));}
int ap_receive(void *p,const uint8_t *data,uint32_t n,uint32_t now){return p&&n<=512?static_cast<Session*>(p)->receive(data,n,now):0;}
int ap_get_snapshot(void *p,uint32_t now,ap_snapshot *out,uint32_t bytes){
  if(!p||!out||bytes!=sizeof(*out))return 0;
  auto &s=static_cast<Session*>(p)->telemetry;
  *out={AP_ABI_VERSION,s.protocol,s.known,s.mainSeen,s.settingsSeen,telemetryFresh(s,now),
    s.soc,s.chargeWatts,s.outputWatts,uint32_t(s.minutes)*60,uint32_t(s.source),s.sourceRaw,
    bool(s.mainFlags&2),bool(s.mainFlags&1),bool(s.settingsFlags&0x10),
    s.mainSeen?uint32_t(now-s.mainAt):UINT32_MAX,s.settingsSeen?uint32_t(now-s.settingsAt):UINT32_MAX,s.validFrames,s.invalidFrames};
  return 1;
}
uint32_t ap_model_hint(const char *name){return uint32_t(AllpowersModels::nameHint(name));}
uint32_t ap_catalog_count(){return AllpowersModels::kRegistryCount;}
const char *ap_catalog_name(uint32_t i){return i<ap_catalog_count()?AllpowersModels::kRegistry[i].advertisedName:nullptr;}
uint32_t ap_catalog_model(uint32_t i){return i<ap_catalog_count()?uint32_t(AllpowersModels::kRegistry[i].model):0;}
const char *ap_uuid(uint32_t kind){return kind==0?AllpowersModels::kServiceUuid:kind==1?AllpowersModels::kNotifyUuid:kind==2?AllpowersModels::kWriteUuid:nullptr;}
uint32_t ap_poll(void *p,uint8_t *out,uint32_t capacity){
  if(!p)return 0;auto request=static_cast<Session*>(p)->poll();
  if(request.length>capacity||(!out&&request.length))return 0;
  if(request.length)memcpy(out,request.bytes,request.length);return request.length;
}
uint32_t ap_poll_interval_ms(){return AllpowersCore::kPollIntervalMs;}
uint32_t ap_reconnect_silence_ms(){return AllpowersCore::kReconnectSilenceMs;}
int ap_self_test(){return AllpowersModels::selfTest()&&telemetrySelfTest();}
