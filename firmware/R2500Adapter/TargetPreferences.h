#pragma once
#include <Preferences.h>
#include "StationPicker.h"

// Explicit one-key NVS operations. The BLE task owns the settings mutex and
// checks BOOT/restart before opening for writes. No hidden background writes.
class TargetPreferences:public StationPicker::BlobStorage {
  Preferences preferences;
public:
  bool begin(bool readOnly){return preferences.begin("ap-target",readOnly);}
  void end(){preferences.end();}
  size_t length()override{return preferences.getBytesLength("last");}
  bool read(void *data,size_t bytes)override{return preferences.getBytes("last",data,bytes)==bytes;}
  bool write(const void *data,size_t bytes)override{return preferences.putBytes("last",data,bytes)==bytes;}
};
