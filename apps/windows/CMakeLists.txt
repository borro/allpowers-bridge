cmake_minimum_required(VERSION 4.4)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(AllpowersMonitor LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/WindowsCompiler.cmake")
include(CTest)
get_filename_component(ALLPOWERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${ALLPOWERS_ROOT}/cmake/Dependencies.cmake")
include("${ALLPOWERS_ROOT}/cmake/Version.cmake")
ap_add_build_identity()
if(NOT ALLPOWERS_QT_SOURCE_CACHE)
  set(ALLPOWERS_QT_SOURCE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/deps/downloads" CACHE PATH "Verified Qt source archives")
endif()
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 ${ALLPOWERS_QT_VERSION} EXACT REQUIRED COMPONENTS Widgets Bluetooth Sql Network)
if(BUILD_TESTING)
  find_package(Qt6 ${ALLPOWERS_QT_VERSION} EXACT REQUIRED COMPONENTS Test)
endif()
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
  transport.h transport_router.cpp connection_lease.cpp usb_serial.cpp usb_transport.cpp)
target_include_directories(monitor_backend PUBLIC . ../../core/native ../../core/src)
target_compile_definitions(monitor_backend PUBLIC NOMINMAX)
target_link_libraries(monitor_backend PUBLIC Qt6::Core Qt6::Bluetooth Qt6::Sql Qt6::Network
  advapi32 bcrypt setupapi cfgmgr32 newdev shell32 wintrust crypt32 ole32 user32)
add_library(monitor_lib STATIC chart.cpp controls.cpp window.cpp notices.cpp monitor_application.cpp)
target_include_directories(monitor_lib PUBLIC . ../../core/native)
target_link_libraries(monitor_lib PUBLIC Qt6::Widgets monitor_backend)
add_library(widget_lib STATIC desktop_surface.cpp widget.cpp)
target_link_libraries(widget_lib PUBLIC monitor_lib)
if(MSVC)
  target_compile_options(widget_lib PRIVATE /utf-8 /W4)
endif()
add_library(protocol STATIC ../../core/native/api.cpp)
target_include_directories(protocol PUBLIC ../../core/src ../../core/native)
target_link_libraries(monitor_backend PUBLIC protocol)
target_compile_features(monitor_backend PUBLIC cxx_std_23)
target_compile_features(protocol PUBLIC cxx_std_23)
# Only version consumers include the generated header; do not force it into
# every backend, UI and test translation unit through PUBLIC compile options.
foreach(target monitor_backend monitor_lib)
  add_dependencies(${target} product-identity)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/identity")
endforeach()
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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${ALLPOWERS_ROOT}/cmake/CollectQtNotices.cmake"
  "${ALLPOWERS_ROOT}/docs/windows-runtime-terms.md" "${ALLPOWERS_ROOT}/docs/licenses/MSVC-STL.md")
qt_add_resources(notice_sources "${CMAKE_CURRENT_BINARY_DIR}/notices.qrc")
target_sources(AllpowersMonitor PRIVATE ${notice_sources})
qt_add_executable(AllpowersWidget WIN32 widget_main.cpp)
target_include_directories(AllpowersWidget PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/identity")
add_dependencies(AllpowersWidget product-identity)
target_sources(AllpowersWidget PRIVATE widget.manifest)
target_sources(AllpowersWidget PRIVATE ${notice_sources})
target_link_libraries(AllpowersWidget PRIVATE widget_lib)
qt_add_resources(AllpowersWidget widget_icons PREFIX "/" FILES assets/allpowers.ico)
if(MSVC)
  target_compile_options(AllpowersWidget PRIVATE /utf-8 /W4)
endif()
if(WIN32)
  enable_language(RC)
  string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" numeric_version "${ALLPOWERS_VERSION}")
  string(REPLACE "." "," ALLPOWERS_RC_VERSION "${numeric_version}")
  foreach(app AllpowersMonitor AllpowersWidget)
    set(ALLPOWERS_EXE_NAME "${app}")
    set(ALLPOWERS_EXE_DESCRIPTION "${app}")
    target_sources(${app} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/identity/${app}.rc")
    add_dependencies(${app} product-identity)
  endforeach()
endif()
target_link_libraries(AllpowersMonitor PRIVATE monitor_lib)
if(BUILD_TESTING)
  add_executable(history_operation_id_probe ../../tests/desktop/history_operation_id_probe.cpp)
  set_target_properties(history_operation_id_probe PROPERTIES AUTOMOC OFF)
  target_include_directories(history_operation_id_probe PRIVATE .)
  target_compile_definitions(history_operation_id_probe PRIVATE NOMINMAX)
  target_compile_features(history_operation_id_probe PRIVATE cxx_std_23)
  target_link_libraries(history_operation_id_probe PRIVATE Qt6::Core bcrypt)
  qt_import_plugins(history_operation_id_probe NO_DEFAULT)
  if(MSVC)
    target_compile_options(history_operation_id_probe PRIVATE /utf-8)
  endif()
  add_test(NAME history-operation-process COMMAND "${CMAKE_COMMAND}"
    "-DPROBE=$<TARGET_FILE:history_operation_id_probe>"
    -P "${ALLPOWERS_ROOT}/tests/cmake/history_operation_id_process_test.cmake")
  set_tests_properties(history-operation-process PROPERTIES TIMEOUT 40)
  qt_add_executable(monitor_tests ../../tests/desktop/monitor_test.cpp)
  target_sources(monitor_tests PRIVATE ${notice_sources})
  target_link_libraries(monitor_tests PRIVATE monitor_lib Qt6::Test)
  target_compile_definitions(monitor_tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures")
  target_link_libraries(monitor_tests PRIVATE widget_lib)
  qt_add_executable(archive_transport_tests
    ../../tests/desktop/archive_transport_test.cpp
    ../../tests/desktop/frozen_b989/usb_transport.cpp
    ../../tests/desktop/frozen_b989/usb_transport.h)
  target_link_libraries(archive_transport_tests PRIVATE monitor_backend Qt6::Test)
  target_compile_definitions(archive_transport_tests PRIVATE
    FROZEN_B989_DIR="${ALLPOWERS_ROOT}/tests/desktop/frozen_b989")
  if(MSVC)
    target_compile_options(archive_transport_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  add_test(NAME archive-transport COMMAND archive_transport_tests
    -v1 -o -,txt -o archive-transport-tests.xml,junitxml)
  qt_add_executable(widget_tests ../../tests/desktop/widget_test.cpp)
  target_sources(widget_tests PRIVATE widget.manifest)
  target_link_libraries(widget_tests PRIVATE widget_lib Qt6::Test)
  if(MSVC)
    target_compile_options(widget_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  add_test(NAME widget COMMAND widget_tests -platform offscreen
    -v1 -o -,txt -o widget-tests.xml,junitxml)
  add_test(NAME widget-native COMMAND widget_tests nativeHostRecovery nativeCardPaintAndDrag -platform windows)

  if(MSVC)
    target_compile_options(monitor_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  add_test(NAME desktop COMMAND monitor_tests -platform offscreen
    -v1 -o -,txt -o desktop-tests.xml,junitxml)
  add_test(NAME dropdown-native COMMAND monitor_tests dropdownFitsVisibleRows:2-0 dropdownFitsVisibleRows:2-1
    -platform windows -o dropdown-native-tests.txt,txt)
  set_tests_properties(dropdown-native PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=1.5")
  set_tests_properties(widget-native dropdown-native PROPERTIES RESOURCE_LOCK windows-desktop)
  add_test(NAME single-exe COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:AllpowersMonitor>"
    "-DIDENTITY_FILE=${ALLPOWERS_IDENTITY_FILE}"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/single-exe-check"
    "-DDRIVER_PACKAGE=${ALLPOWERS_DRIVER_PACKAGE_DIR}"
    -P "${ALLPOWERS_ROOT}/cmake/CheckSingleExe.cmake")
  add_test(NAME widget-single-exe COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:AllpowersWidget>"
    "-DIDENTITY_FILE=${ALLPOWERS_IDENTITY_FILE}"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/widget-single-exe-check" -DWIDGET=ON
    -P "${ALLPOWERS_ROOT}/cmake/CheckSingleExe.cmake")

  # These offline suites normally finish in seconds. A deadlock or modal UI
  # must fail with diagnostics instead of hiding inside the parent CTest job.
  set_tests_properties(archive-transport PROPERTIES TIMEOUT 60)
  set_tests_properties(widget widget-native desktop dropdown-native single-exe widget-single-exe
    PROPERTIES TIMEOUT 120)
endif()
set(plugin_targets AllpowersMonitor AllpowersWidget)
if(BUILD_TESTING)
  list(APPEND plugin_targets monitor_tests widget_tests archive_transport_tests)
endif()
foreach(target ${plugin_targets})
  qt_import_plugins(${target} NO_DEFAULT INCLUDE
    Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin
    Qt6::QSQLiteDriverPlugin Qt6::QICOPlugin)
endforeach()
install(TARGETS AllpowersMonitor AllpowersWidget RUNTIME DESTINATION .)
