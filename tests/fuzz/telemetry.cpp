#include "AllpowersCore.h"
#include "HidTelemetryWire.h"
#include "HistoryClock.h"
#include "HistorySample.h"
#include "TelemetryRecovery.h"
#include "UpsState.h"
#include <array>
#include <cassert>
#include <tuple>

namespace {
auto values(const Telemetry &t) {
  return std::tie(t.mainAt, t.settingsAt, t.mainSeen, t.settingsSeen, t.soc, t.mainFlags,
    t.settingsFlags, t.chargeWatts, t.outputWatts, t.minutes, t.sourceRaw, t.source,
    t.lastKnownSource, t.protocol, t.known);
}
void check(const AllpowersCore::Session &s, uint32_t now) {
  const auto &t = s.telemetry;
  const bool mainCurrent = t.mainSeen && uint32_t(now - t.mainAt) <= 15000;
  const bool settingsCurrent = t.settingsSeen && t.source != PowerSource::Unknown &&
                               uint32_t(now - t.settingsAt) <= 15000;
  const bool expected = mainCurrent && (s.model != AllpowersModels::Model::R2500V2 || settingsCurrent);
  assert(s.fresh(now) == expected);
  assert(upsTelemetryFresh(t, now) == (expected && s.model == AllpowersModels::Model::R2500V2));
  AllpowersModels::Decoded main, settings;
  main.model = settings.model = s.model;
  main.kind = t.mainSeen ? AllpowersModels::Kind::Main : AllpowersModels::Kind::None;
  settings.kind = t.settingsSeen ? AllpowersModels::Kind::Settings : AllpowersModels::Kind::None;
  main.known = settings.known = t.known;
  main.receivedAtMs = t.mainAt; settings.receivedAtMs = t.settingsAt;
  const auto required = AllpowersModels::Soc | AllpowersModels::RuntimeMinutes | AllpowersModels::MainsSource;
  assert(AllpowersModels::existingUpsEligible(main, settings, now) ==
         (expected && s.model == AllpowersModels::Model::R2500V2 && (t.known & required) == required));
  const auto sample = HistoryFormat::sample(t, now);
  assert(bool(sample.flags & HistoryFormat::Fresh) == expected);
  assert(HistoryFormat::known(sample.magic) == (expected ? t.known : 0));
  assert(sample.soc == (expected ? t.soc : 255));
  assert(sample.outputWatts == (expected ? t.outputWatts : 0xffff));
  assert(!(sample.flags & HistoryFormat::Mains) || t.source == PowerSource::Mains);
  assert(!(sample.flags & HistoryFormat::Battery) || t.source == PowerSource::Battery);
  const auto poll = s.poll();
  assert((poll.length != 0) == (s.model == AllpowersModels::Model::R2500V2));
}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 16) return 0;
  uint32_t now = HidTelemetry::get32(data);
  AllpowersCore::Session session(AllpowersModels::Model(data[4] % 4));
  AllpowersCore::TelemetryRecovery recovery;
  recovery.begin(now); uint32_t lastComplete = now;
  HistoryFormat::ClockAnchor clock;
  uint32_t clockBase = 0;
  AllpowersUps::Cache ups;
  ups.nextTag = HidTelemetry::get32(data + 8);
  uint64_t driverNow = 0;
  constexpr uint32_t jumps[] = {0, 1, 14999, 15000, 15001, 45000, 45001, UINT32_MAX};
  for (size_t at = 16, step = 0; at + 16 <= size && step < 64; at += 16, ++step) {
    const auto *p = data + at;
    now += jumps[p[1] % 8]; driverNow += jumps[p[1] % 7];
    if (p[0] % 8 <= 3) {
      const auto before = session.telemetry;
      std::array<uint8_t, 16> packet{};
      size_t length = 0;
      const uint8_t *bytes = packet.data();
      if (p[0] % 8 == 0) {
        packet = {0xa5,0x65,0xb1,0,1,8,1,p[2],uint8_t(p[3] % 101),p[4],p[5],p[6],p[7],p[8],p[9],0};
        length = 16;
      } else if (p[0] % 8 == 1) {
        const uint16_t sources[] = {0, 0xffff, 1};
        const auto source = sources[p[3] % 3];
        packet = {0xa5,0x65,0xb1,0,1,6,3,p[2],1,uint8_t(source >> 8),uint8_t(source),3,2,0,0,0};
        length = 14;
      } else if (p[0] % 8 == 2) {
        bytes = p; length = p[15] % 17;
      } else { bytes = nullptr; length = p[15]; }
      if (length && bytes == packet.data())
        for (size_t i = 0; i + 1 < length; ++i) packet[length - 1] ^= packet[i];
      const bool accepted = session.receive(bytes, length, now);
      const auto &after = session.telemetry;
      assert(after.validFrames == before.validFrames + unsigned(accepted));
      assert(after.invalidFrames == before.invalidFrames + unsigned(!accepted));
      if (!accepted) assert(values(before) == values(after));
      else {
        assert(after.soc <= 100 && !(after.known & ~0xfffu));
        if (after.protocol != uint8_t(AllpowersModels::Model::R2500V2))
          assert(after.source == PowerSource::Unknown && after.lastKnownSource == PowerSource::Unknown);
        if (bytes == packet.data() && length == 14 && session.model == AllpowersModels::Model::R2500V2) {
          assert(after.source == (p[3] % 3 == 0 ? PowerSource::Mains :
                                 p[3] % 3 == 1 ? PowerSource::Battery : PowerSource::Unknown));
          assert(bool(after.known & AllpowersModels::MainsSource) == (p[3] % 3 != 2));
        }
      }
    } else if (p[0] % 8 == 4) {
      session.reset(AllpowersModels::Model(p[2] % 4));
      assert(!session.fresh(now) && !session.telemetry.mainSeen && !session.telemetry.settingsSeen);
      assert(!session.telemetry.validFrames && !session.telemetry.invalidFrames && !session.telemetry.known);
      recovery.begin(now); lastComplete = now;
      clock = {}; clockBase = 0;
    } else if (p[0] % 8 == 5) {
      const uint32_t epoch = p[2] & 1 ? HidTelemetry::get32(p + 4) : 1800000000u;
      const uint32_t observed = p[3] & 1 ? HidTelemetry::get32(p + 8) : now;
      const uint64_t observedNow = uint64_t(now) + p[12];
      uint32_t base = 0;
      const bool accepted = clock.candidate(epoch, observed, observedNow, base);
      const int64_t difference = int64_t(observedNow) - observed;
      const bool valid = epoch >= 1577836800u && observed < epoch && difference >= 0 &&
                         difference <= 5 && int64_t(epoch) + difference <= UINT32_MAX;
      assert(accepted == valid);
      if (accepted) { clock.commit(base); if (!clockBase) clockBase = epoch - observed; }
      assert(clock.synced() == (clockBase != 0));
      const uint64_t uptime = p[13] & 1 ? uint64_t(UINT32_MAX) : observedNow;
      const uint64_t expected = uint64_t(clockBase) + uptime;
      assert(clock.at(uptime) == (clockBase && expected <= UINT32_MAX ? expected : 0));
    } else if (p[0] % 8 == 6) {
      using namespace AllpowersUps;
      if (p[2] & 1) {
        ups.release();
        assert(!ups.tag && !ups.deadline && !ups.state.generation && !ups.state.sequence);
      }
      else {
        State candidate;
        candidate.generation = p[3] & 1 ? ups.state.generation + 1 : p[4];
        candidate.sequence = p[5] & 1 ? ups.state.sequence + 1 : p[6];
        constexpr uint32_t flags[] = {0, Present, Present | Mains, Present | Mains | Charging,
                                      Present | Discharging | RuntimeKnown};
        candidate.flags = flags[p[7] % 5]; candidate.percent = p[8] % 101;
        candidate.validForMs = 1 + HidTelemetry::get16(p + 9) % MaxAgeMs;
        candidate.runtimeSeconds = HidTelemetry::get16(p + 11);
        const bool malformed = p[14] % 14 != 0;
        switch (p[14] % 14) {
        case 1: candidate.size = 0; break;
        case 2: candidate.version = 0; break;
        case 3: candidate.generation = 0; break;
        case 4: candidate.sequence = 0; break;
        case 5: candidate.flags |= 32; break;
        case 6: candidate.percent = 101; break;
        case 7: candidate.validForMs = MaxAgeMs + 1; break;
        case 8: candidate.flags |= RuntimeKnown; candidate.runtimeSeconds = 0x80000000u; break;
        case 9: candidate.flags |= Present; candidate.validForMs = 0; break;
        case 10: candidate.flags = Present | Charging | Discharging; break;
        case 11: candidate.flags = Present | Charging; break;
        case 12: candidate.flags = Present | Mains | Discharging; break;
        case 13: candidate.flags = Mains; break;
        default: break;
        }
        const State previous = ups.state;
        const auto oldTag = ups.tag, oldNext = ups.nextTag;
        const auto oldDeadline = ups.deadline;
        const bool expected = !malformed && candidate.generation && candidate.sequence &&
          (candidate.generation > previous.generation ||
           (candidate.generation == previous.generation && candidate.sequence > previous.sequence));
        assert(ups.accept(candidate, driverNow) == expected);
        if (!expected) {
          assert(ups.state.generation == previous.generation && ups.state.sequence == previous.sequence);
          assert(ups.tag == oldTag && ups.nextTag == oldNext && ups.deadline == oldDeadline);
        } else {
          assert(ups.state.percent == candidate.percent);
          assert(ups.present(driverNow) == bool(candidate.flags & Present));
          assert(!ups.present(driverNow + candidate.validForMs));
        }
      }
      const bool wasPresent = ups.present(driverNow);
      const bool tagged = ups.tag != 0;
      assert(ups.expire(driverNow) == (tagged && !wasPresent));
      assert(!ups.expire(driverNow));
    } else {
      std::array<char, 17> name{};
      for (unsigned i = 0; i < 16; ++i) name[i] = char(p[i]);
      const auto hint = AllpowersModels::nameHint(name.data());
      assert(unsigned(hint) <= unsigned(AllpowersModels::Model::R2500V2));
      assert(AllpowersModels::nameHint(nullptr) == AllpowersModels::Model::Unknown);
      assert(AllpowersModels::nameHint("AP R2500 V2.0") == AllpowersModels::Model::R2500V2);
    }
    check(session, now);
    recovery.observe(now, session.fresh(now));
    if (session.fresh(now)) lastComplete = now;
    assert(recovery.reconnectDue(now) == (uint32_t(now - lastComplete) > 45000));
  }
  return 0;
}
