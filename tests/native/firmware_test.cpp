#include <array>
#include <cassert>
#include <initializer_list>
#include "BootGestures.h"
#include "StationPicker.h"
#include "HistoryRing.h"
#include "GraphHistory.h"
#include "HistoryWire.h"
#include "TelemetryLabels.h"
#include "HistoryExport.h"
#include "output_test.h"
#include "history_operation_test.h"
#include "firmware_contract_test.h"
#include "ble_disconnect_test.h"

int main() {
  assert(BootGestures::selfTest());
  assert(StationPicker::selfTest());
  std::array<uint8_t, HistoryFormat::kBytes> flash{};
  assert(HistoryFormat::selfTest(flash.data()));
  assert(HistoryFormat::wireSelfTest(flash.data()));
  assert(HistoryFormat::exportSelfTest());
  OutputTests::selfTest();
  HistoryOperationTests::selfTest();
  assert(telemetryLabelsSelfTest());
  std::array<HistoryFormat::Record, GraphHistory::kCapacity> graph{};
  assert(GraphHistory::selfTest(graph.data()));
  FirmwareContractTests::run();
  BleDisconnectTests::run();
}
