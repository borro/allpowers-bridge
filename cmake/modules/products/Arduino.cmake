include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../sdk/Arduino.cmake")

function(ap_add_firmware_targets)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(PROJECT_SOURCE_DIR "${AP_ROOT}")
  set(PROJECT_BINARY_DIR "${AP_BUILD_ROOT}")
  ap_configure_arduino(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}")
  # Reconfigure for added/removed inputs; build dependencies follow the actual inventory.
  file(GLOB_RECURSE firmware_candidates CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/core/src/*" "${PROJECT_SOURCE_DIR}/firmware/R2500Adapter/*")
  list(APPEND firmware_candidates "${PROJECT_SOURCE_DIR}/core/library.properties.in")
  # A removed dependency vanishes from Ninja's graph; no remaining source needs
  # to become newer than the existing manifest. Make membership itself an input.
  # Relative, sorted paths keep the inventory stable in clones and source kits.
  set(firmware_inventory)
  set(firmware_sources)
  foreach(path IN LISTS firmware_candidates)
    file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${path}")
    if(relative MATCHES "(^|/)(__pycache__|build|data)/")
      continue()
    endif()
    list(APPEND firmware_sources "${path}")
    list(APPEND firmware_inventory "${relative}")
  endforeach()
  list(SORT firmware_inventory)
  string(REPLACE ";" "\n" firmware_inventory "${firmware_inventory}")
  set(firmware_inventory_file "${PROJECT_BINARY_DIR}/firmware/source-inventory.txt")
  file(GENERATE OUTPUT "${firmware_inventory_file}" CONTENT "${firmware_inventory}\n")
  set(board_number 0)
  foreach(board waveshare-147 pocket-096)
    math(EXPR board_number "${board_number}+1")
    set(output "${ALLPOWERS_RELEASE_DIR}/firmware/${board}")
    add_custom_command(OUTPUT "${output}/manifest.json"
      BYPRODUCTS "${output}/R2500Adapter.ino.bin" "${output}/R2500Adapter.ino.bootloader.bin"
        "${output}/R2500Adapter.ino.partitions.bin" "${output}/boot_app0.bin"
        "${output}/corresponding-source.zip" "${output}/FIRMWARE-LICENSES.md"
        "${output}/licenses/README.md" "${output}/licenses/NOTICES.md" "${output}/licenses/components.json"
      COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DBOARD=${board}"
        "-DBOARD_NUMBER=${board_number}" "-DCLI=${ARDUINO_CLI}" "-DDATA=${ARDUINO_DATA_DIR}"
        "-DCONFIG=${arduino_config}" "-DWORK=${PROJECT_BINARY_DIR}/firmware/${board}"
        "-DJOBS=${ALLPOWERS_ARDUINO_JOBS}"
        "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}"
        "-DLEGAL_CACHE=${ALLPOWERS_DEPS_DIR}/firmware-legal"
        "-DOUTPUT=${output}" "-DIDENTITY_FILE=${ALLPOWERS_IDENTITY_FILE}"
        -P "${PROJECT_SOURCE_DIR}/cmake/scripts/project/BuildFirmware.cmake"
      DEPENDS arduino-setup ${firmware_sources} "${firmware_inventory_file}" "${PROJECT_SOURCE_DIR}/tests/fixtures/usb-contract.json"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/hid-telemetry-contract.json"
        "${arduino_config}" "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        "${PROJECT_SOURCE_DIR}/cmake/scripts/project/BuildFirmware.cmake" "${PROJECT_SOURCE_DIR}/cmake/modules/release/Partitions.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/scripts/quality/CheckContracts.cmake" "${PROJECT_SOURCE_DIR}/cmake/modules/common/Json.cmake"
        "${firmware_dependencies_file}" "${ALLPOWERS_IDENTITY_FILE}"
        "${PROJECT_SOURCE_DIR}/cmake/modules/release/StageFirmware.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/modules/release/HistoryCompatibility.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/scripts/release/PrepareFirmwareKit.cmake" "${PROJECT_SOURCE_DIR}/cmake/scripts/release/RelinkFirmware.cmake"
        "${PROJECT_SOURCE_DIR}/cmake/modules/release/CollectFirmwareNotices.cmake" "${PROJECT_SOURCE_DIR}/tools/release/firmware_notices.py"
        "${PROJECT_SOURCE_DIR}/docs/licenses/TLSF-BSD-3-Clause.md"
        "${PROJECT_SOURCE_DIR}/docs/firmware-licenses.md"
        "${PROJECT_SOURCE_DIR}/docs/licenses/LGPL-3.0.md" "${PROJECT_SOURCE_DIR}/docs/licenses/GPL-3.0.md"
      JOB_POOL firmware VERBATIM)
    add_custom_target(${board} DEPENDS "${output}/manifest.json")
    add_dependencies(${board} product-identity)
  endforeach()
  add_custom_target(firmware DEPENDS waveshare-147 pocket-096)
endfunction()
