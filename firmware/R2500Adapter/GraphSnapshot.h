#pragma once
#include "GraphHistory.h"
#include <string.h>

namespace GraphSnapshot {
struct Input {
  GraphHistory::RingView archive, live;
  uint32_t now = 0, session = 0;
};
struct View {
  const GraphHistory::Point *points = nullptr;
  size_t count = 0;
  bool unknown = false;
};
inline bool fits(const Input &in, const GraphHistory::Point *memory, size_t capacity) {
  return memory && in.archive.valid() && in.live.valid() && in.archive.count <= capacity &&
         in.live.count <= capacity - in.archive.count;
}
// Reference path retained for diagnostic comparisons: copy oldest-first, clear
// age/flags, then make the original independent age-assignment passes.
inline View legacy(GraphHistory::Point *memory, size_t capacity, const Input &in) {
  if (!fits(in, memory, capacity))
    return {};
  size_t n = 0;
  for (uint32_t i = 0; i < in.archive.count; ++i) {
    const auto &r = in.archive.at(i);
    if (in.live.count && r.session == in.live.at(0).session &&
        r.uptimeSeconds >= in.live.at(0).uptimeSeconds)
      continue;
    memory[n++] = {r, 0, false, false};
  }
  for (uint32_t i = 0; i < in.live.count; ++i)
    memory[n++] = {in.live.at(i), 0, false, false};
  if (!n)
    return {};
  return {memory, n, GraphHistory::assignAges(memory, n, in.now, in.session)};
}
class ReverseCursor {
  GraphHistory::RingView view;
  uint32_t index = 0, left = 0;

public:
  explicit ReverseCursor(GraphHistory::RingView source) : view(source), left(source.count) {
    if (left)
      index = (source.head + source.count - 1) % source.capacity;
  }
  bool pop(HistoryFormat::Record &record) {
    if (!left)
      return false;
    record = view.data[index];
    --left;
    index = index ? index - 1 : view.capacity - 1;
    return true;
  }
};
// Build into the END of the caller's allocation. Returning a view avoids a
// counting pass or memmove when overlapping minute records are discarded.
// Keep the pending newer point in internal RAM until its predecessor is known:
// age, join and unknown-boundary fields are written to PSRAM exactly once.
inline View build(GraphHistory::Point *memory, size_t capacity, const Input &in) {
  if (!fits(in, memory, capacity))
    return {};
  GraphHistory::Point pending{};
  bool have = false, unknown = false;
  size_t cursor = capacity;
  auto older = [&](const HistoryFormat::Record &r) {
    if (!have) {
      unknown = r.session != in.session;
      pending = {r, !unknown && in.now >= r.uptimeSeconds ? in.now - r.uptimeSeconds : in.now,
                 false, unknown};
      have = true;
      return;
    }
    const auto &b = pending.sample;
    uint32_t delta = 0;
    if (r.session == b.session && b.uptimeSeconds >= r.uptimeSeconds)
      delta = b.uptimeSeconds - r.uptimeSeconds;
    else if ((r.flags & HistoryFormat::Clock) && (b.flags & HistoryFormat::Clock) &&
             b.epochSeconds >= r.epochSeconds)
      delta = b.epochSeconds - r.epochSeconds;
    else {
      delta = b.uptimeSeconds;
      pending.unknownBefore = true;
      unknown = true;
    }
    const uint64_t age = uint64_t(pending.age) + delta;
    pending.joinPrevious = GraphHistory::join(r, b);
    memcpy(memory + --cursor, &pending, sizeof(pending));
    pending = {r, age > GraphHistory::kHorizon ? GraphHistory::kHorizon + 1 : uint32_t(age), false,
               false};
  };
  ReverseCursor recent(in.live);
  HistoryFormat::Record r;
  while (recent.pop(r))
    older(r);
  const uint32_t liveSession = in.live.count ? in.live.at(0).session : 0;
  const uint32_t liveStart = in.live.count ? in.live.at(0).uptimeSeconds : 0;
  ReverseCursor archived(in.archive);
  while (archived.pop(r)) {
    if (in.live.count && r.session == liveSession && r.uptimeSeconds >= liveStart)
      continue;
    older(r);
  }
  if (!have)
    return {};
  pending.joinPrevious = false;
  memcpy(memory + --cursor, &pending, sizeof(pending));
  return {memory + cursor, capacity - cursor, unknown};
}
// Freeze only occupied physical runs, preserving wrap/heads for a paired test.
inline bool freeze(GraphHistory::RingView source, HistoryFormat::Record *dest, size_t capacity) {
  if (!source.valid() || source.capacity > capacity)
    return false;
  if (!source.count)
    return true;
  if (!dest)
    return false;
  const size_t first =
      source.count < source.capacity - source.head ? source.count : source.capacity - source.head;
  memcpy(dest + source.head, source.data + source.head, first * sizeof(*dest));
  if (source.count > first)
    memcpy(dest, source.data, (source.count - first) * sizeof(*dest));
  return true;
}
} // namespace GraphSnapshot
