#include "api.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static std::vector<uint8_t> hex(const std::string &text) {
  std::vector<uint8_t> bytes;
  assert(text.size() % 2 == 0);
  for (size_t i = 0; i < text.size(); i += 2)
    bytes.push_back(static_cast<uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
  return bytes;
}
struct Session {
  void *handle = ap_create(3);
  ~Session() {
    ap_destroy(handle);
  }
  bool receive(const std::vector<uint8_t> &data, uint32_t now) {
    return ap_receive(handle, data.data(), static_cast<uint32_t>(data.size()), now) != 0;
  }
  ap_snapshot snapshot(uint32_t now) {
    ap_snapshot result{};
    assert(ap_get_snapshot(handle, now, &result, sizeof result));
    assert(result.abi_version == 1);
    return result;
  }
};
int main() {
  assert(ap_abi_version() == 1 && ap_self_test());
  {
    // The exported C boundary must reject invalid handles/buffer sizes without
    // touching a caller's output, and expose a self-consistent model catalog.
    ap_destroy(nullptr);
    ap_reset(nullptr, 3);
    assert(!ap_receive(nullptr, nullptr, 0, 0));
    assert(!ap_poll(nullptr, nullptr, 0));
    Session s;
    ap_snapshot sentinel{};
    sentinel.soc = 77;
    assert(!ap_get_snapshot(s.handle, 0, nullptr, sizeof sentinel));
    assert(!ap_get_snapshot(s.handle, 0, &sentinel, sizeof sentinel + 1));
    assert(sentinel.soc == 77);
    assert(!ap_receive(s.handle, nullptr, 513, 0));
    assert(!ap_receive(s.handle, nullptr, 0, 0));
    uint8_t small[2] = {0x12, 0x34};
    assert(!ap_poll(s.handle, small, sizeof small));
    assert(small[0] == 0x12 && small[1] == 0x34);
    ap_reset(s.handle, 3);
    assert(!ap_poll(s.handle, nullptr, 512));
    ap_reset(s.handle, UINT32_MAX);
    assert(s.snapshot(0).model == 0 && !s.snapshot(0).fresh);
    for (uint32_t i = 0; i < ap_catalog_count(); ++i) {
      const auto *name = ap_catalog_name(i);
      assert(name && *name);
      assert(ap_catalog_model(i) >= 1 && ap_catalog_model(i) <= 3);
      assert(ap_model_hint(name) == ap_catalog_model(i));
    }
    assert(!ap_catalog_name(ap_catalog_count()));
    assert(!ap_catalog_model(UINT32_MAX));
    for (uint32_t kind = 0; kind < 3; ++kind)
      assert(ap_uuid(kind) && std::string(ap_uuid(kind)).size() == 36);
    assert(std::string(ap_uuid(1)) != ap_uuid(2));
    assert(!ap_uuid(3));
    assert(ap_poll_interval_ms() > 0);
    assert(ap_reconnect_silence_ms() > ap_poll_interval_ms());
  }
  const auto main = hex("a565b1000108010264000000e696006f");
  const auto mains = hex("a565b10001060300010000030275");
  const auto battery = hex("a565b1000106030001ffff030275");
  {
    Session s;
    assert(s.receive(main, 100) && !s.snapshot(100).fresh);
    assert(s.receive(mains, 200));
    const auto v = s.snapshot(201);
    assert(v.soc == 100 && v.input_w == 0 && v.output_w == 230);
    assert(v.runtime_s == 2304000 && v.source == 2 && v.fresh);
    assert(!s.snapshot(15201).fresh);
    assert(s.receive(battery, 15202) && s.snapshot(15202).source == 1);
    auto unknown = mains;
    unknown[9] = 1;
    unknown.back() ^= 1;
    assert(s.receive(unknown, 15203));
    assert(s.snapshot(15203).source == 0 && !s.snapshot(15203).fresh);
  }
  {
    Session s;
    assert(s.receive(main, 100));
    for (size_t n = 0; n < main.size(); ++n)
      assert(!s.receive(std::vector<uint8_t>(main.begin(), main.begin() + n), 200));
    for (size_t n = 0; n < main.size(); ++n) {
      auto corrupted = main;
      corrupted[n] ^= 1;
      assert(!s.receive(corrupted, 200));
    }
    auto v = s.snapshot(201);
    assert(v.soc == 100 && v.main_age_ms == 101 && v.invalid_frames == 32);
    ap_reset(s.handle, 0);
    assert(s.receive(main, 100));
    v = s.snapshot(101);
    assert(v.model == 2 && v.source == 0 && !(v.known & 64));
    uint8_t poll[32]{};
    assert(ap_poll(s.handle, poll, sizeof poll) == 0);
    ap_reset(s.handle, 1);
    assert(s.receive(hex("0f2a006400c800070441"), 100));
    v = s.snapshot(101);
    assert(v.soc == 42 && v.runtime_s == 65340 && v.ac_output && v.usb_output && v.dc_output);
  }
  {
    Session s;
    assert(s.receive(main, 0xfffffff0) && s.receive(mains, 0xfffffff0));
    assert(s.snapshot(30).fresh);
    ap_reset(s.handle, 3);
    assert(!s.snapshot(31).main_seen && !s.snapshot(31).fresh && s.snapshot(31).valid_frames == 0);
    assert(ap_catalog_count() == 17 && ap_model_hint("AP R2500 V2.0") == 3);
    assert(ap_model_hint("unknown") == 0);
    uint8_t poll[32]{};
    const auto expected = hex("a565b1000106010000000000");
    assert(ap_poll(s.handle, poll, sizeof poll) == expected.size());
    assert(std::equal(expected.begin(), expected.end(), poll));
    ap_snapshot out{};
    assert(!ap_get_snapshot(s.handle, 0, &out, sizeof out - 1));
    assert(!ap_get_snapshot(nullptr, 0, &out, sizeof out));
  }
  {
    Session s;
    std::ifstream input(FIXTURE_PATH);
    assert(input);
    const std::string json{std::istreambuf_iterator<char>(input), {}};
    uint32_t accepted = 0, now = 0;
    for (size_t pos = json.find('"'); pos != std::string::npos;) {
      const auto end = json.find('"', pos + 1);
      assert(end != std::string::npos);
      accepted += s.receive(hex(json.substr(pos + 1, end - pos - 1)), now);
      now += 100;
      pos = json.find('"', end + 1);
    }
    assert(accepted > 20 && s.snapshot(now).valid_frames == accepted);
  }
  std::cout << "PASS: shared C ABI, packet fixtures, stale/unknown data, faults, wrap and bounds\n";
}
