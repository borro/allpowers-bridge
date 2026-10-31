#include <array>
#include <cassert>
#include "HistoryWire.h"
#include "archive_contract_test.h"
#include "ble_disconnect_test.h"
int main() {
  std::array<uint8_t, HistoryFormat::kBytes> flash{};
  assert(HistoryFormat::selfTest(flash.data()));
  assert(HistoryFormat::wireSelfTest(flash.data()));
  ArchiveContractTests::run();
  BleDisconnectTests::run();
}
