#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Portable protocol: does not register USB, connect BLE, or send commands.
// Protocol facts: madninjaskillz/allpowers-ble main
// 7e67941b17f3f199a28e64952436a809aa03eb46 and draft PR2
// 7f11e74931657be8d1660ef4c0eccc43eba34771. See THIRD_PARTY_NOTICES.md.
// Upstream MIT notice is in AllpowersModels.LICENSE.txt.
// R2500V2 source mapping below is this project's accepted hardware mapping;
// it is deliberately never applied to other/unknown models.
namespace AllpowersModels {
enum class Model : uint8_t { Unknown, Old10, Legacy16, R2500V2 };
enum class Evidence : uint8_t { IssueCatalog, IssueUserReport, LocalHardware };
struct RegistryEntry { const char *advertisedName; const char *catalogId; Model model; Evidence evidence; };
// Catalog facts from issue1's Power_BLE.html.txt attachment, not a claim that
// main implements/tests all these devices. catalogId is NOT an advertised-name
// alias: several generations reuse the same ID and cannot be disambiguated by it.
static const RegistryEntry kRegistry[]={
  {"ALLPOWERS S400","AP-SS-005",Model::Old10,Evidence::IssueCatalog},
  {"ALLPOWERS S500","AP-SS-007",Model::Old10,Evidence::IssueCatalog},
  {"ALLPOWERS S1000","AP-SS-008",Model::Old10,Evidence::IssueCatalog},
  {"ALLPOWERS S2000","AP-SS-009",Model::Old10,Evidence::IssueCatalog},
  {"AP S300 V2.0","AP-SS-005",Model::Legacy16,Evidence::IssueCatalog},
  {"AP S700 V2.0","AP-SS-007",Model::Legacy16,Evidence::IssueUserReport},
  {"AP S1500 V2.0","AP-SS-008",Model::Legacy16,Evidence::IssueCatalog},
  {"AP S2000 V2.0","AP-SS-009",Model::Legacy16,Evidence::IssueCatalog},
  {"AP S2000P V2.0","AP-SS-009 PRO",Model::Legacy16,Evidence::IssueCatalog},
  {"AP R600 V2.0","AP-SS-005PRO",Model::Legacy16,Evidence::IssueCatalog},
  {"AP R700 V2.0","AP-SS-007PRO",Model::Legacy16,Evidence::IssueCatalog},
  {"AP S1500MI V2.0","AP-SS-009MINI",Model::Legacy16,Evidence::IssueCatalog},
  {"AP R3500 V2.0","AP-SS-010MAX",Model::Legacy16,Evidence::IssueCatalog},
  {"AP R2500 V2.0","AP-SS-010PRO",Model::R2500V2,Evidence::LocalHardware},
  {"AP R1500 V2.0","AP-SS-010",Model::Legacy16,Evidence::IssueCatalog},
  {"AP R4000 V2.0","AP-SS-011",Model::Legacy16,Evidence::IssueCatalog},
  {"AP B3000 V2.0","AP-EB-003",Model::Legacy16,Evidence::IssueCatalog}
};
constexpr size_t kRegistryCount=sizeof(kRegistry)/sizeof(kRegistry[0]);
enum class Source : uint8_t { Unknown, Battery, Mains };
enum class Kind : uint8_t { None, Main, Settings };
enum class Result : uint8_t { Decoded, Invalid, UnsupportedFrame };
enum Field : uint32_t {
  Soc=1u<<0, InputWatts=1u<<1, OutputWatts=1u<<2, RuntimeMinutes=1u<<3,
  AcOutput=1u<<4, DcOutput=1u<<5, UsbOutput=1u<<6, LightOutput=1u<<7,
  SourceRaw=1u<<8, MainsSource=1u<<9, CycleCount=1u<<10, ChargeTimeRaw=1u<<11
};
struct Decoded {
  Model model=Model::Unknown;
  Kind kind=Kind::None;
  uint32_t known=0, receivedAtMs=0;
  uint8_t soc=0, mainFlags=0, settingsFlags=0;
  uint16_t inputWatts=0, outputWatts=0, runtimeMinutes=0, sourceRaw=0, cycleCount=0, chargeTimeRaw=0;
  bool acOutput=false, dcOutput=false, usbOutput=false, lightOutput=false;
  Source source=Source::Unknown;
  bool has(Field f) const { return (known&uint32_t(f))!=0; }
  uint32_t runtimeSeconds() const { return uint32_t(runtimeMinutes)*60u; }
};
static const char kServiceUuid[]="0000fff0-0000-1000-8000-00805f9b34fb";
static const char kNotifyUuid[]="0000fff1-0000-1000-8000-00805f9b34fb"; // device -> host
static const char kWriteUuid[]="0000fff2-0000-1000-8000-00805f9b34fb";  // host -> device

// A hint only: never excludes other connectable devices from the picker.
// Selecting another device must clear old telemetry and bind a new session.
inline Model nameHint(const char *name) {
  if(!name)return Model::Unknown;
  for(size_t i=0;i<kRegistryCount;++i)if(!strcmp(name,kRegistry[i].advertisedName))return kRegistry[i].model;
  return Model::Unknown;
}
inline uint16_t be16(const uint8_t *p) { return uint16_t(uint16_t(p[0])<<8)|p[1]; }
inline bool framed(const uint8_t *p,size_t n) {
  if(!p || n<8 || p[0]!=0xa5 || p[1]!=0x65 || n!=size_t(p[5])+8) return false;
  uint8_t check=0; for(size_t i=0;i<n;++i) check^=p[i];
  return check==0;
}

// Input is ONE complete GATT notification frame. No stream reassembly and no
// treating unknown/short notifications as zero telemetry. On failure out is
// unchanged. On success out is a delta: Main and Settings have separate ages.
inline Result decode(Model selected,const uint8_t *p,size_t n,uint32_t now,Decoded &out) {
  if(selected==Model::Old10){
    // No header/checksum is specified by the old-protocol source. Never use
    // length alone to autodetect this protocol on an arbitrary BLE device.
    if(!p||n!=10||p[1]>100)return Result::Invalid;
    Decoded d;d.model=selected;d.kind=Kind::Main;d.receivedAtMs=now;
    d.mainFlags=p[0];d.soc=p[1];d.inputWatts=be16(p+2);d.outputWatts=be16(p+4);
    d.cycleCount=be16(p+6);d.runtimeMinutes=be16(p+8);
    d.dcOutput=(p[0]&1)!=0;d.usbOutput=(p[0]&2)!=0;d.acOutput=(p[0]&4)!=0;d.lightOutput=(p[0]&8)!=0;
    d.known=Soc|InputWatts|OutputWatts|RuntimeMinutes|CycleCount|DcOutput|UsbOutput|AcOutput|LightOutput;
    out=d;return Result::Decoded;
  }
  if(!framed(p,n)) return Result::Invalid;
  if(selected!=Model::Unknown && selected!=Model::Legacy16 && selected!=Model::R2500V2)
    return Result::UnsupportedFrame;
  if(selected==Model::R2500V2 && (p[2]!=0xb1 || p[3]!=0 || p[4]!=1)) return Result::Invalid;
  Decoded d;
  d.model=selected==Model::Unknown?Model::Legacy16:selected;
  d.receivedAtMs=now;
  if(p[6]==1 && n==16) {
    if(p[8]>100) return Result::Invalid;
    d.kind=Kind::Main; d.mainFlags=p[7]; d.soc=p[8];
    d.inputWatts=be16(p+9); d.outputWatts=be16(p+11); d.runtimeMinutes=be16(p+13);
    d.acOutput=(p[7]&2)!=0;
    d.known=Soc|InputWatts|OutputWatts|RuntimeMinutes|AcOutput;
    if(selected==Model::R2500V2) {
      d.usbOutput=(p[7]&1)!=0; d.known|=UsbOutput;
    } else {
      d.dcOutput=(p[7]&1)!=0; d.lightOutput=(p[7]&0x10)!=0;
      d.known|=DcOutput|LightOutput;
    }
  } else if(selected==Model::R2500V2 && p[6]==3 && n==14) {
    d.kind=Kind::Settings; d.settingsFlags=p[7];
    d.dcOutput=(p[7]&0x10)!=0; d.sourceRaw=be16(p+9);
    d.known=DcOutput|SourceRaw;
    d.source=d.sourceRaw==0xffff ? Source::Battery : d.sourceRaw==0 ? Source::Mains : Source::Unknown;
    if(d.source!=Source::Unknown) d.known|=MainsSource;
    // Merge ALL Settings results, including Unknown source, to invalidate an
    // earlier known source. Do not retain MainsSource just because has()==false.
  } else if(selected==Model::Legacy16 && p[6]==3 && n==14) {
    // Issue attachment calls this raw field chargeTime. It does not prove
    // mains presence, charging direction, or units for every catalog model.
    d.kind=Kind::Settings;d.settingsFlags=p[7];d.chargeTimeRaw=be16(p+9);
    d.known=ChargeTimeRaw;
  } else return Result::UnsupportedFrame;
  out=d; return Result::Decoded;
}

struct PollView { const uint8_t *bytes; size_t length; };
inline PollView readOnlyPoll(Model model) {
  // Existing, hardware-tested R2500V2 status request from this project.
  // Not claimed to be provided by upstream; NOT a universal Allpowers query.
  static const uint8_t request[]={0xa5,0x65,0xb1,0,1,6,1,0,0,0,0,0};
  return model==Model::R2500V2 ? PollView{request,sizeof(request)} : PollView{nullptr,0};
}

// Deliberately conservative eligibility for the EXISTING USB UPS descriptor,
// which advertises ACPresent/Charging/Discharging. Generic telemetry alone
// must not masquerade as a full UPS with an invented source state.
inline bool existingUpsEligible(const Decoded &main,const Decoded &settings,uint32_t now,uint32_t maxAge=15000) {
  return main.model==Model::R2500V2 && settings.model==Model::R2500V2 &&
    main.kind==Kind::Main && settings.kind==Kind::Settings &&
    main.has(Soc) && main.has(RuntimeMinutes) && settings.has(MainsSource) &&
    uint32_t(now-main.receivedAtMs)<=maxAge && uint32_t(now-settings.receivedAtMs)<=maxAge;
}

// Small allocation-free startup gate. The host test adds malformed-frame cases.
inline bool selfTest(){
  const uint8_t realMain[]={0xa5,0x65,0xb1,0,1,8,1,2,100,0,0,0,230,0x96,0,0x6f};
  const uint8_t realSettings[]={0xa5,0x65,0xb1,0,1,6,3,0,1,0,0,3,2,0x75};
  const uint8_t prMain[]={0xa5,0x65,0xb1,0,1,8,1,2,0x2a,0,0,0,5,4,0x41,0x11};
  const uint8_t oldSynthetic[]={0x0f,42,0,100,0,200,0,7,4,0x41};
  Decoded main,settings,generic,old;
  if(kRegistryCount!=17||nameHint("AP R2500 V2.0")!=Model::R2500V2||nameHint("ALLPOWERS S500")!=Model::Old10)return false;
  if(decode(Model::R2500V2,realMain,16,100,main)!=Result::Decoded||main.soc!=100||main.runtimeSeconds()!=2304000)return false;
  if(decode(Model::R2500V2,realSettings,14,100,settings)!=Result::Decoded||!existingUpsEligible(main,settings,101))return false;
  if(decode(Model::Unknown,realMain,16,100,generic)!=Result::Decoded||generic.has(MainsSource)||generic.has(UsbOutput))return false;
  if(decode(Model::Legacy16,realSettings,14,100,generic)!=Result::Decoded||generic.has(MainsSource)||!generic.has(ChargeTimeRaw))return false;
  if(decode(Model::R2500V2,prMain,16,100,main)!=Result::Decoded||main.soc!=42||main.runtimeMinutes!=1089)return false;
  if(decode(Model::Old10,oldSynthetic,10,100,old)!=Result::Decoded||old.cycleCount!=7||old.runtimeMinutes!=1089||old.source!=Source::Unknown)return false;
  if(decode(Model::Unknown,oldSynthetic,10,100,generic)!=Result::Invalid)return false;
  return readOnlyPoll(Model::Old10).length==0&&readOnlyPoll(Model::Legacy16).length==0&&readOnlyPoll(Model::R2500V2).length==12;
}
} // namespace AllpowersModels
