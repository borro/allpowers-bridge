set(ARDUINO_CLI "$ENV{ARDUINO_CLI}" CACHE FILEPATH "Arduino CLI 1.5.1 executable")
set(ARDUINO_DATA_DIR "$ENV{ARDUINO_DATA_DIR}" CACHE PATH "Arduino package data")
set(ALLPOWERS_ARDUINO_JOBS 4 CACHE STRING "Compiler jobs per Arduino build")
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
  file(READ "${local_config}" settings)
  string(JSON saved_platform ERROR_VARIABLE error GET "${settings}" platform)
  if(error OR saved_platform STREQUAL host_platform)
    foreach(pair "ARDUINO_CLI;cli" "ARDUINO_DATA_DIR;data")
      list(GET pair 0 variable)
      list(GET pair 1 key)
      string(JSON value ERROR_VARIABLE error GET "${settings}" "${key}")
      if(NOT error AND NOT ${variable})
        set(${variable} "${value}" CACHE PATH "Local Arduino setting" FORCE)
      endif()
    endforeach()
  endif()
endif()
if(NOT ARDUINO_CLI)
  find_program(detected_cli NAMES arduino-cli)
  if(detected_cli)
    set(ARDUINO_CLI "${detected_cli}" CACHE FILEPATH "Arduino CLI" FORCE)
  else()
    set(ARDUINO_CLI "${PROJECT_SOURCE_DIR}/build/deps/arduino-cli/${CMAKE_HOST_SYSTEM_NAME}/arduino-cli${cli_suffix}"
      CACHE FILEPATH "Arduino CLI" FORCE)
  endif()
endif()
if(NOT ARDUINO_DATA_DIR)
  set(ARDUINO_DATA_DIR "${PROJECT_SOURCE_DIR}/build/deps/arduino/${CMAKE_HOST_SYSTEM_NAME}/data"
    CACHE PATH "Arduino package data" FORCE)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/Json.cmake")
file(TO_CMAKE_PATH "${ARDUINO_DATA_DIR}" data)
ap_json_quote(data "${data}")
ap_json_quote(downloads "${PROJECT_SOURCE_DIR}/build/deps/arduino/${CMAKE_HOST_SYSTEM_NAME}/downloads")
ap_json_quote(user "${PROJECT_BINARY_DIR}/arduino/user")
set(arduino_config "${PROJECT_BINARY_DIR}/arduino/config.json")
file(GENERATE OUTPUT "${arduino_config}" CONTENT
  "{\"directories\":{\"data\":${data},\"downloads\":${downloads},\"user\":${user}},\"board_manager\":{\"additional_urls\":[\"https://espressif.github.io/arduino-esp32/package_esp32_index.json\"]}}\n")
add_custom_target(arduino-setup COMMAND "${CMAKE_COMMAND}"
  "-DCLI=${ARDUINO_CLI}" "-DDATA=${ARDUINO_DATA_DIR}" "-DCONFIG=${arduino_config}"
  -P "${PROJECT_SOURCE_DIR}/cmake/SetupArduino.cmake" USES_TERMINAL VERBATIM)
file(GLOB_RECURSE firmware_sources CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/core/src/*" "${PROJECT_SOURCE_DIR}/firmware/R2500Adapter/*")
list(APPEND firmware_sources "${PROJECT_SOURCE_DIR}/core/library.properties.in")
set(board_number 0)
foreach(board waveshare-147 pocket-096)
  math(EXPR board_number "${board_number}+1")
  set(output "${ALLPOWERS_RELEASE_DIR}/firmware/${board}")
  add_custom_command(OUTPUT "${output}/manifest.json"
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DBOARD=${board}"
      "-DBOARD_NUMBER=${board_number}" "-DCLI=${ARDUINO_CLI}" "-DDATA=${ARDUINO_DATA_DIR}"
      "-DCONFIG=${arduino_config}" "-DWORK=${PROJECT_BINARY_DIR}/firmware/${board}"
      "-DJOBS=${ALLPOWERS_ARDUINO_JOBS}"
      "-DOUTPUT=${output}" "-DVERSION=${ALLPOWERS_VERSION}"
      -P "${PROJECT_SOURCE_DIR}/cmake/BuildFirmware.cmake"
    DEPENDS ${firmware_sources} "${PROJECT_SOURCE_DIR}/tests/fixtures/usb-contract.json"
      "${PROJECT_SOURCE_DIR}/cmake/BuildFirmware.cmake" "${PROJECT_SOURCE_DIR}/cmake/Partitions.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/CheckContracts.cmake" "${PROJECT_SOURCE_DIR}/cmake/Json.cmake"
    USES_TERMINAL VERBATIM)
  add_custom_target(${board} DEPENDS "${output}/manifest.json")
endforeach()
