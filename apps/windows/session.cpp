#include "session.h"
Session::Session() {
  handle = ap_create(0);
  if (!handle)
    qFatal("Cannot create protocol session");
}
Session::~Session() {
  ap_destroy(handle);
}
quint64 Session::reset(int model) {
  ++generation_;
  ap_reset(handle, quint32(model));
  return generation_;
}
bool Session::receive(quint64 generation, const QByteArray &data, quint32 now) {
  return generation == generation_ &&
         ap_receive(handle, reinterpret_cast<const uint8_t *>(data.constData()),
                    uint32_t(data.size()), now);
}
ap_snapshot Session::snapshot(quint32 now) const {
  ap_snapshot s{};
  ap_get_snapshot(handle, now, &s, sizeof(s));
  return s;
}
QByteArray Session::poll() const {
  char data[64];
  auto n = ap_poll(handle, reinterpret_cast<uint8_t *>(data), sizeof(data));
  return QByteArray(data, int(n));
}
