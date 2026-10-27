set(ARDUINO_CLI "$ENV{ARDUINO_CLI}" CACHE FILEPATH "Arduino CLI 1.5.1 executable")
set(ARDUINO_DATA_DIR "$ENV{ARDUINO_DATA_DIR}" CACHE PATH "Arduino package data")
set(ALLPOWERS_ARDUINO_JOBS 2 CACHE STRING "Compiler jobs per Arduino build")
set(ALLPOWERS_FIRMWARE_PARALLEL_BOARDS 2 CACHE STRING "Concurrent firmware builds")
foreach(variable ALLPOWERS_ARDUINO_JOBS ALLPOWERS_FIRMWARE_PARALLEL_BOARDS)
  if(NOT ${variable} MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "${variable} must be a positive integer")
  endif()
endforeach()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS "firmware=${ALLPOWERS_FIRMWARE_PARALLEL_BOARDS}")
if(CMAKE_HOST_WIN32)
  set(host_platform win32)
  set(cli_suffix .exe)
else()
  set(host_platform linux)
  set(cli_suffix "")
endif()
set(local_config "${PROJECT_SOURCE_DIR}/.local/arduino.${host_platform}.json")
if(NOT EXISTS "${local_config}")
  set(local_config "${PROJECT_SOURCE_DIR}/.local/arduino.json")
endif()
if(EXISTS "${local_config}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${local_config}")
  file(READ "${local_config}" settings)
  string(JSON saved_platform ERROR_VARIABLE error GET "${settings}" platform)
  if(error OR saved_platform STREQUAL host_platform)
    foreach(pair "ARDUINO_CLI;cli" "ARDUINO_DATA_DIR;data")
      list(GET pair 0 variable)
      list(GET pair 1 key)
      string(JSON value ERROR_VARIABLE error GET "${settings}" "${key}")
      if(NOT error AND NOT ${variable})
        if(variable STREQUAL ARDUINO_CLI)
          set(${variable} "${value}" CACHE FILEPATH "Local Arduino executable" FORCE)
        else()
          set(${variable} "${value}" CACHE PATH "Local Arduino package data" FORCE)
        endif()
      endif()
    endforeach()
  endif()
endif()
if(NOT ARDUINO_CLI)
  find_program(detected_cli NAMES arduino-cli NO_CACHE)
  if(detected_cli)
    set(ARDUINO_CLI "${detected_cli}" CACHE FILEPATH "Arduino CLI" FORCE)
  else()
    set(ARDUINO_CLI "${ALLPOWERS_DEPS_DIR}/arduino-cli/${CMAKE_HOST_SYSTEM_NAME}/arduino-cli${cli_suffix}"
      CACHE FILEPATH "Arduino CLI" FORCE)
  endif()
endif()
if(NOT ARDUINO_DATA_DIR)
  set(ARDUINO_DATA_DIR "${ALLPOWERS_DEPS_DIR}/arduino/${CMAKE_HOST_SYSTEM_NAME}/data"
    CACHE PATH "Arduino package data" FORCE)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/Json.cmake")
file(TO_CMAKE_PATH "${ARDUINO_DATA_DIR}" data)
ap_json_quote(data "${data}")
ap_json_quote(downloads "${ALLPOWERS_DEPS_DIR}/arduino/${CMAKE_HOST_SYSTEM_NAME}/downloads")
ap_json_quote(user "${PROJECT_BINARY_DIR}/arduino/user")
set(arduino_config "${PROJECT_BINARY_DIR}/arduino/config.json")
file(GENERATE OUTPUT "${arduino_config}" CONTENT
  "{\"directories\":{\"data\":${data},\"downloads\":${downloads},\"user\":${user}},\"board_manager\":{\"additional_urls\":[\"https://espressif.github.io/arduino-esp32/package_esp32_index.json\"]}}\n")
include("${CMAKE_CURRENT_LIST_DIR}/DependencyFingerprint.cmake")
ap_dependency_fingerprint(firmware-legal firmware_dependencies)
set(firmware_dependencies_file "${PROJECT_BINARY_DIR}/firmware/dependencies.txt")
file(GENERATE OUTPUT "${firmware_dependencies_file}" CONTENT "${firmware_dependencies}\n")
add_custom_target(arduino-setup COMMAND "${CMAKE_COMMAND}"
  "-DCLI=${ARDUINO_CLI}" "-DDATA=${ARDUINO_DATA_DIR}" "-DCONFIG=${arduino_config}"
  -P "${PROJECT_SOURCE_DIR}/cmake/SetupArduino.cmake" USES_TERMINAL VERBATIM)
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
      "${output}/LICENSE" "${output}/THIRD_PARTY_NOTICES.md" "${output}/AllpowersModels.LICENSE.txt"
      "${output}/ESP32-Arduino-LICENSE.md" "${output}/corresponding-source.zip"
      "${output}/licenses.zip" "${output}/FIRMWARE-LICENSES.md"
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DBOARD=${board}"
      "-DBOARD_NUMBER=${board_number}" "-DCLI=${ARDUINO_CLI}" "-DDATA=${ARDUINO_DATA_DIR}"
      "-DCONFIG=${arduino_config}" "-DWORK=${PROJECT_BINARY_DIR}/firmware/${board}"
      "-DJOBS=${ALLPOWERS_ARDUINO_JOBS}"
      "-DLEGAL_CACHE=${ALLPOWERS_DEPS_DIR}/firmware-legal"
      "-DOUTPUT=${output}" "-DIDENTITY_FILE=${ALLPOWERS_IDENTITY_FILE}"
      -P "${PROJECT_SOURCE_DIR}/cmake/BuildFirmware.cmake"
    DEPENDS arduino-setup ${firmware_sources} "${firmware_inventory_file}" "${PROJECT_SOURCE_DIR}/tests/fixtures/usb-contract.json"
      "${arduino_config}" "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
      "${PROJECT_SOURCE_DIR}/cmake/BuildFirmware.cmake" "${PROJECT_SOURCE_DIR}/cmake/Partitions.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/CheckContracts.cmake" "${PROJECT_SOURCE_DIR}/cmake/Json.cmake"
      "${firmware_dependencies_file}" "${ALLPOWERS_IDENTITY_FILE}"
      "${PROJECT_SOURCE_DIR}/cmake/StageFirmware.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/HistoryCompatibility.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/PrepareFirmwareKit.cmake" "${PROJECT_SOURCE_DIR}/cmake/RelinkFirmware.cmake"
      "${PROJECT_SOURCE_DIR}/docs/firmware-licenses.md"
      "${PROJECT_SOURCE_DIR}/docs/licenses/LGPL-3.0.md" "${PROJECT_SOURCE_DIR}/docs/licenses/GPL-3.0.md"
    JOB_POOL firmware VERBATIM)
  add_custom_target(${board} DEPENDS "${output}/manifest.json")
  add_dependencies(${board} product-identity)
endforeach()
add_custom_target(firmware DEPENDS waveshare-147 pocket-096)
