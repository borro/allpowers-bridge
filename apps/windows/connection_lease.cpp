#include "connection_lease.h"
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <windows.h>
namespace {
QMutex guard;
QSet<QString> held;
} // namespace
ConnectionLease::ConnectionLease(QString key) : name("Local\\" + key) {}
ConnectionLease::~ConnectionLease() {
  release();
}
bool ConnectionLease::acquire() {
  QMutexLocker locker(&guard);
  if (handle)
    return true;
  if (held.contains(name))
    return false;
  HANDLE candidate = CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(name.utf16()));
  if (!candidate)
    return false;
  DWORD result = WaitForSingleObject(candidate, 0);
  if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
    CloseHandle(candidate);
    return false;
  }
  handle = candidate;
  held.insert(name);
  return true;
}
void ConnectionLease::release() {
  QMutexLocker locker(&guard);
  if (!handle)
    return;
  ReleaseMutex(handle);
  CloseHandle(handle);
  handle = nullptr;
  held.remove(name);
}
