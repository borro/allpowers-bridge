#include "AllpowersModels.h"
#include "TelemetryRecovery.h"
#include "AllpowersCore.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>
using namespace AllpowersModels;
static void checksum(uint8_t *p, size_t n) {
  p[n - 1] = 0;
  for (size_t i = 0; i < n - 1; ++i)
    p[n - 1] ^= p[i];
}
int main() {
  AllpowersCore::TelemetryRecovery recovery;
  recovery.begin(100);
  // Initial silence, main-only, settings-only and missing RX all remain
  // incomplete: packets alone cannot reset the recovery deadline.
  for (uint32_t now=100;now<=45100;now+=1000) {
    recovery.observe(now,false);
    assert(!recovery.reconnectDue(now));
  }
  assert(recovery.reconnectDue(45101));
  recovery.observe(46000,true);
  assert(!recovery.reconnectDue(91000));
  assert(recovery.reconnectDue(91001));
  recovery.begin(UINT32_MAX-1000);
  assert(!recovery.reconnectDue(1000));
  assert(recovery.reconnectDue(45000));
  assert(selfTest());
  // Actual local R2500V2 captures: SOC100, input0W, output230W, 38400 minutes.
  const uint8_t ownMain[] = {0xa5, 0x65, 0xb1, 0, 1, 8, 1, 2, 100, 0, 0, 0, 230, 0x96, 0, 0x6f};
  const uint8_t ownSettings[] = {0xa5, 0x65, 0xb1, 0, 1, 6, 3, 0, 1, 0, 0, 3, 2, 0x75};
  for(bool mainOnly : {false,true}) {
    AllpowersCore::Session session(Model::R2500V2);
    recovery.begin(0);
    for(uint32_t now=0;now<=46000;now+=1000) {
      assert(mainOnly ? session.receive(ownMain,sizeof(ownMain),now)
                      : session.receive(ownSettings,sizeof(ownSettings),now));
      recovery.observe(now,session.fresh(now));
    }
    assert(recovery.reconnectDue(46000));
    assert(session.receive(ownMain,sizeof(ownMain),46001));
    assert(session.receive(ownSettings,sizeof(ownSettings),46001));
    recovery.observe(46001,session.fresh(46001));
    assert(!recovery.reconnectDue(46002));
  }
  // Real public PR2 capture: https://github.com/madninjaskillz/allpowers-ble/pull/2
  const uint8_t prMain[] = {0xa5, 0x65, 0xb1, 0, 1, 8, 1, 2, 0x2a, 0, 0, 0, 5, 4, 0x41, 0x11};
  const uint8_t prSettings[] = {0xa5, 0x65, 0xb1, 0, 1, 6, 3, 0, 0, 0, 0, 0x14, 0x30, 0x51};
  Decoded main, settings, generic;
  {
    Decoded unchanged;
    unchanged.soc = 77;
    assert(decode(static_cast<Model>(255), ownMain, sizeof ownMain, 100, unchanged) ==
           Result::UnsupportedFrame);
    assert(unchanged.soc == 77);
    // Generic Settings packets must accumulate their supported fields without
    // claiming the R2500-specific mains/battery interpretation.
    Telemetry telemetry;
    auto model = Model::Legacy16;
    assert(decodeStationTelemetry(telemetry, ownMain, sizeof ownMain, 100, model));
    const auto mainKnown = telemetry.known;
    assert(decodeStationTelemetry(telemetry, ownSettings, sizeof ownSettings, 200, model));
    assert(telemetry.settingsSeen && telemetry.settingsAt == 200);
    assert((telemetry.known & mainKnown) == mainKnown);
    assert(telemetry.source == PowerSource::Unknown && !(telemetry.known & MainsSource));
    assert(telemetry.validFrames == 2 && telemetryFresh(telemetry, 201));
  }
  assert(decode(Model::R2500V2, ownMain, 16, 100, main) == Result::Decoded);
  assert(main.soc == 100 && main.inputWatts == 0 && main.outputWatts == 230 &&
         main.runtimeSeconds() == 2304000);
  assert(main.has(UsbOutput) && !main.has(DcOutput) && !main.has(MainsSource));
  assert(decode(Model::R2500V2, ownSettings, 14, 100, settings) == Result::Decoded);
  assert(settings.source == Source::Mains && existingUpsEligible(main, settings, 101));
  assert(!existingUpsEligible(main, settings, 15101));
  assert(decode(Model::Unknown, ownMain, 16, 100, generic) == Result::Decoded);
  assert(generic.model == Model::Legacy16 && generic.has(DcOutput) && !generic.has(UsbOutput));
  assert(generic.source == Source::Unknown && !generic.has(MainsSource));
  assert(!existingUpsEligible(generic, settings, 101));
  assert(decode(Model::Unknown, ownSettings, 14, 100, generic) == Result::UnsupportedFrame);
  assert(readOnlyPoll(Model::Unknown).length == 0 && readOnlyPoll(Model::Legacy16).length == 0);
  assert(readOnlyPoll(Model::R2500V2).length == 12);
  assert(nameHint("AP R2500 V2.0") == Model::R2500V2 &&
         nameHint("AP R600 V2.0") == Model::Legacy16);
  assert(nameHint(nullptr) == Model::Unknown);
  assert(decode(Model::R2500V2, prMain, 16, 100, main) == Result::Decoded);
  assert(main.soc == 42 && main.outputWatts == 5 && main.runtimeMinutes == 1089);
  assert(decode(Model::R2500V2, prSettings, 14, 100, settings) == Result::Decoded);
  // Synthetic fault/bit fixtures below; these are NOT additional hardware captures.
  uint8_t altered[16];
  memcpy(altered, ownMain, 16);
  altered[7] = 0x13;
  checksum(altered, 16);
  assert(decode(Model::Legacy16, altered, 16, 100, generic) == Result::Decoded);
  assert(generic.acOutput && generic.dcOutput && generic.lightOutput && !generic.has(MainsSource));
  assert(decode(Model::R2500V2, altered, 16, 100, main) == Result::Decoded && main.usbOutput &&
         !main.has(LightOutput));
  for (size_t n = 0; n < 16; ++n)
    assert(decode(Model::Unknown, ownMain, n, 100, generic) != Result::Decoded);
  assert(decode(Model::Unknown, nullptr, 16, 100, generic) == Result::Invalid);
  altered[8] = 101;
  checksum(altered, 16);
  assert(decode(Model::Unknown, altered, 16, 100, generic) == Result::Invalid);
  memcpy(altered, ownMain, 16);
  altered[15] ^= 1;
  assert(decode(Model::Unknown, altered, 16, 100, generic) == Result::Invalid);
  memcpy(altered, ownMain, 16);
  altered[0] = 0;
  checksum(altered, 16);
  assert(decode(Model::Unknown, altered, 16, 100, generic) == Result::Invalid);
  memcpy(altered, ownSettings, 14);
  altered[9] = 0xff;
  altered[10] = 0xff;
  checksum(altered, 14);
  assert(decode(Model::R2500V2, altered, 14, 100, settings) == Result::Decoded &&
         settings.source == Source::Battery);
  assert(decode(Model::Legacy16, altered, 14, 100, generic) == Result::Decoded &&
         !generic.has(MainsSource) && generic.source == Source::Unknown);
  altered[9] = 1;
  altered[10] = 0;
  checksum(altered, 14);
  assert(decode(Model::R2500V2, altered, 14, 100, settings) == Result::Decoded &&
         !settings.has(MainsSource));
  assert(settings.source == Source::Unknown && !existingUpsEligible(main, settings, 101));
  puts("PASS: actual R2500/local+PR2 fixtures, generic mapping, unknown-source gate, "
       "malformed/truncated inputs");
}
