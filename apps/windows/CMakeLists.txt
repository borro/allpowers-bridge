cmake_minimum_required(VERSION 3.25)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(AllpowersMonitor LANGUAGES CXX)
get_filename_component(ALLPOWERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT ALLPOWERS_VERSION)
  include("${ALLPOWERS_ROOT}/cmake/Version.cmake")
endif()
if(NOT ALLPOWERS_QT_SOURCE_CACHE)
  set(ALLPOWERS_QT_SOURCE_CACHE "${ALLPOWERS_ROOT}/build/deps/downloads" CACHE PATH "Verified Qt source archives")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Bluetooth Sql Network Test)
get_target_property(qt_core_type Qt6::Core TYPE)
if(NOT qt_core_type STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "Use a static Qt SDK, or build the root windows target to bootstrap Qt")
endif()
if(WIN32 AND NOT MSVC)
  message(FATAL_ERROR "Windows BLE needs the MSVC Qt build; MinGW has a dummy Bluetooth backend")
endif()
get_filename_component(qt_prefix "${Qt6Bluetooth_DIR}/../../.." ABSOLUTE)
set(bluetooth_config "${qt_prefix}/include/QtBluetooth/${Qt6Bluetooth_VERSION}/QtBluetooth/private/qtbluetooth-config_p.h")
if(NOT EXISTS "${bluetooth_config}")
  message(FATAL_ERROR "Cannot verify Qt's WinRT Bluetooth feature configuration")
endif()
file(READ "${bluetooth_config}" bluetooth_features)
if(NOT bluetooth_features MATCHES "#define QT_FEATURE_winrt_bt 1")
  message(FATAL_ERROR "Qt Bluetooth must contain the real WinRT backend")
endif()
add_library(monitor_backend STATIC session.cpp history.cpp bluetooth.cpp ipc.cpp service_host.cpp service_client.cpp integration.cpp battery_probe.cpp
  transport.h transport_router.cpp usb_serial.cpp usb_transport.cpp)
target_include_directories(monitor_backend PUBLIC . ../../core/native ../../core/src)
target_compile_definitions(monitor_backend PUBLIC NOMINMAX)
target_link_libraries(monitor_backend PUBLIC Qt6::Core Qt6::Bluetooth Qt6::Sql Qt6::Network
  advapi32 setupapi cfgmgr32 newdev shell32 wintrust crypt32 ole32 user32)
add_library(monitor_lib STATIC chart.cpp controls.cpp window.cpp notices.cpp)
target_include_directories(monitor_lib PUBLIC . ../../core/native)
target_link_libraries(monitor_lib PUBLIC Qt6::Widgets monitor_backend)
add_library(protocol STATIC ../../core/native/api.cpp)
target_include_directories(protocol PUBLIC ../../core/src ../../core/native)
target_link_libraries(monitor_backend PUBLIC protocol)
# Keep project-header invalidation deterministic even with localized MSVC
# /showIncludes output (some Windows toolchains do not report a usable prefix).
file(GLOB_RECURSE model_headers CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/windows/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../core/src/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../core/native/*.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${model_headers})
set(header_digests "")
foreach(header IN LISTS model_headers)
  file(SHA256 "${header}" digest)
  string(APPEND header_digests "${digest}")
endforeach()
string(SHA256 header_digest "${header_digests}")
target_compile_definitions(monitor_lib PUBLIC ALLPOWERS_HEADER_HASH="${header_digest}")
target_compile_definitions(monitor_backend PUBLIC ALLPOWERS_HEADER_HASH="${header_digest}" APP_VERSION="${ALLPOWERS_VERSION}")
target_compile_definitions(protocol PRIVATE ALLPOWERS_HEADER_HASH="${header_digest}")
target_compile_definitions(monitor_lib PUBLIC APP_VERSION="${ALLPOWERS_VERSION}")
if(MSVC)
  target_compile_options(monitor_lib PRIVATE /utf-8 /W4)
  target_compile_options(monitor_backend PRIVATE /utf-8 /W4)
  target_compile_options(protocol PRIVATE /utf-8)
endif()
qt_add_executable(AllpowersMonitor WIN32 main.cpp)
qt_add_resources(AllpowersMonitor icons PREFIX "/" FILES assets/allpowers.ico assets/logo.png)
if(ALLPOWERS_DRIVER_PACKAGE_DIR)
  foreach(name AllpowersUps.sys AllpowersUps.inf AllpowersUps.cat)
    if(NOT EXISTS "${ALLPOWERS_DRIVER_PACKAGE_DIR}/${name}")
      message(FATAL_ERROR "Driver payload missing: ${name}; build root ups-driver first")
    endif()
    # Inf2Cat writes a lowercase filename; Qt resource lookup is case-sensitive
    # even on Windows. Keep the public resource names deterministic.
    set_source_files_properties("${ALLPOWERS_DRIVER_PACKAGE_DIR}/${name}"
      PROPERTIES QT_RESOURCE_ALIAS "${name}")
  endforeach()
  qt_add_resources(AllpowersMonitor driver_payload PREFIX "/driver"
    BASE "${ALLPOWERS_DRIVER_PACKAGE_DIR}" FILES
    "${ALLPOWERS_DRIVER_PACKAGE_DIR}/AllpowersUps.sys"
    "${ALLPOWERS_DRIVER_PACKAGE_DIR}/AllpowersUps.inf"
    "${ALLPOWERS_DRIVER_PACKAGE_DIR}/AllpowersUps.cat")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DROOT=${ALLPOWERS_ROOT}"
  "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}"
  -P "${ALLPOWERS_ROOT}/cmake/PrepareNotices.cmake" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${ALLPOWERS_ROOT}/THIRD_PARTY_NOTICES.md" "${ALLPOWERS_ROOT}/docs/relinking.md"
  "${ALLPOWERS_ROOT}/cmake/PrepareNotices.cmake" "${ALLPOWERS_ROOT}/cmake/Dependencies.cmake")
qt_add_resources(notice_sources "${CMAKE_CURRENT_BINARY_DIR}/notices.qrc")
target_sources(AllpowersMonitor PRIVATE ${notice_sources})
if(WIN32)
  enable_language(RC)
  configure_file(app.rc.in app.rc @ONLY)
  target_sources(AllpowersMonitor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()
target_link_libraries(AllpowersMonitor PRIVATE monitor_lib)
enable_testing()
qt_add_executable(monitor_tests ../../tests/desktop/monitor_test.cpp)
target_link_libraries(monitor_tests PRIVATE monitor_lib Qt6::Test)
target_compile_definitions(monitor_tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures")
foreach(target AllpowersMonitor monitor_tests)
  qt_import_plugins(${target} NO_DEFAULT INCLUDE
    Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin
    Qt6::QSQLiteDriverPlugin Qt6::QICOPlugin)
endforeach()
if(MSVC)
  target_compile_options(monitor_tests PRIVATE /utf-8 /UNDEBUG)
endif()
add_test(NAME desktop COMMAND monitor_tests -platform offscreen -o desktop-tests.txt,txt)
add_test(NAME dropdown-native COMMAND monitor_tests dropdownFitsVisibleRows:2-0 dropdownFitsVisibleRows:2-1
  -platform windows -o dropdown-native-tests.txt,txt)
set_tests_properties(dropdown-native PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=1.5")
add_test(NAME single-exe COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:AllpowersMonitor>"
  "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/single-exe-check"
  "-DDRIVER_PACKAGE=${ALLPOWERS_DRIVER_PACKAGE_DIR}"
  -P "${ALLPOWERS_ROOT}/cmake/CheckSingleExe.cmake")
install(TARGETS AllpowersMonitor RUNTIME DESTINATION .)
