cmake_minimum_required(VERSION 4.4)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(AllpowersMonitor LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/WindowsCompiler.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/SizeOptimization.cmake")
option(ALLPOWERS_OPTIMIZE_SIZE "Optimize Windows applications for size" ON)
include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/Coverage.cmake")
get_filename_component(ALLPOWERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${ALLPOWERS_ROOT}/cmake/Dependencies.cmake")
include("${ALLPOWERS_ROOT}/cmake/StaticAnalysis.cmake")
include("${ALLPOWERS_ROOT}/cmake/Version.cmake")
ap_add_build_identity()
if(NOT ALLPOWERS_QT_SOURCE_CACHE)
  set(ALLPOWERS_QT_SOURCE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/deps/downloads" CACHE PATH "Verified Qt source archives")
endif()
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 ${ALLPOWERS_QT_VERSION} EXACT REQUIRED COMPONENTS Widgets Bluetooth Sql Network LinguistTools)
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
add_library(monitor_backend STATIC i18n.cpp session.cpp history.cpp bluetooth.cpp ipc.cpp service_host.cpp service_client.cpp integration.cpp battery_probe.cpp
  transport.h transport_router.cpp connection_lease.cpp usb_serial.cpp usb_transport.cpp)
target_include_directories(monitor_backend PUBLIC . ../../core/native ../../core/src)
target_compile_definitions(monitor_backend PUBLIC NOMINMAX)
target_link_libraries(monitor_backend PUBLIC Qt6::Core Qt6::Bluetooth Qt6::Sql Qt6::Network
  advapi32 bcrypt setupapi cfgmgr32 newdev shell32 wintrust crypt32 ole32 user32)
add_library(monitor_lib STATIC chart.cpp controls.cpp window.cpp notices.cpp monitor_application.cpp i18n_controls.cpp)
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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ALLPOWERS_ROOT}/docs/licenses/UPX.md")
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
file(GLOB allpowers_i18n_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
set(allpowers_translation_sources translations/allpowers_ru.ts
  translations/allpowers_zh_CN.ts translations/allpowers_zh_TW.ts)
qt_add_lupdate(SOURCE_TARGETS monitor_lib widget_lib monitor_backend AllpowersMonitor AllpowersWidget
  SOURCES ${allpowers_i18n_headers}
  TS_FILES ${allpowers_translation_sources} LUPDATE_TARGET update-translations
  OPTIONS -no-obsolete)
qt_add_lrelease(TS_FILES ${allpowers_translation_sources}
  OPTIONS -fail-on-unfinished
  QM_FILES_OUTPUT_VARIABLE allpowers_qm_files)
# Attach resources to the EXEs explicitly: static-library dead stripping must
# never discard the catalogs. Tests use the identical embedded resource.
set(i18n_targets AllpowersMonitor AllpowersWidget)
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
  qt_add_executable(integration_contract_tests ../../tests/desktop/integration_contract_test.cpp)
  target_include_directories(integration_contract_tests PRIVATE . ../../core/native ../../core/src)
  target_compile_features(integration_contract_tests PRIVATE cxx_std_23)
  target_compile_definitions(integration_contract_tests PRIVATE NOMINMAX QT_FORCE_ASSERTS)
  target_link_libraries(integration_contract_tests PRIVATE Qt6::Core Qt6::Test)
  # Inert payload, never a real driver; native installation APIs are replaced by
  # the closed fake boundary in this test translation unit.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/inert-driver.txt" "Offline integration contract fixture\n")
  foreach(name AllpowersUps.sys AllpowersUps.inf AllpowersUps.cat)
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/inert-driver.txt"
      "${CMAKE_CURRENT_BINARY_DIR}/inert-driver/${name}" COPYONLY)
  endforeach()
  qt_add_resources(integration_contract_tests inert_driver PREFIX "/driver"
    BASE "${CMAKE_CURRENT_BINARY_DIR}/inert-driver" FILES
    "${CMAKE_CURRENT_BINARY_DIR}/inert-driver/AllpowersUps.sys"
    "${CMAKE_CURRENT_BINARY_DIR}/inert-driver/AllpowersUps.inf"
    "${CMAKE_CURRENT_BINARY_DIR}/inert-driver/AllpowersUps.cat")
  qt_import_plugins(integration_contract_tests NO_DEFAULT)
  if(MSVC)
    target_compile_options(integration_contract_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  ap_enable_coverage(integration_contract_tests)
  add_test(NAME integration-contract COMMAND integration_contract_tests
    -o -,txt -o integration-contract-tests.xml,junitxml)
  set_tests_properties(integration-contract PROPERTIES LABELS coverage TIMEOUT 120)
  qt_add_executable(usb_serial_contract_tests ../../tests/desktop/usb_serial_contract_test.cpp usb_serial.h)
  target_include_directories(usb_serial_contract_tests PRIVATE . "${CMAKE_CURRENT_BINARY_DIR}/serial-contract")
  target_compile_features(usb_serial_contract_tests PRIVATE cxx_std_23)
  target_compile_definitions(usb_serial_contract_tests PRIVATE NOMINMAX QT_FORCE_ASSERTS)
  target_link_libraries(usb_serial_contract_tests PRIVATE Qt6::Core Qt6::Test)
  qt6_generate_moc(usb_serial.cpp "${CMAKE_CURRENT_BINARY_DIR}/serial-contract/usb_serial.moc"
    TARGET usb_serial_contract_tests)
  # The included generated moc is skipped by AUTOMOC but appears in its depfile.
  # Generate it before autogen to avoid a dependency cycle on subsequent builds.
  set_property(TARGET usb_serial_contract_tests APPEND PROPERTY AUTOGEN_TARGET_DEPENDS
    "${CMAKE_CURRENT_BINARY_DIR}/serial-contract/usb_serial.moc")
  # qt_generate_moc marks its input SKIP_AUTOMOC globally. The production
  # backend still needs its own moc and automatic discovery of usb_serial.h.
  set_source_files_properties(usb_serial.cpp PROPERTIES SKIP_AUTOMOC OFF)
  target_sources(usb_serial_contract_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/serial-contract/usb_serial.moc")
  qt_import_plugins(usb_serial_contract_tests NO_DEFAULT)
  if(MSVC)
    target_compile_options(usb_serial_contract_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  ap_enable_coverage(usb_serial_contract_tests)
  add_test(NAME usb-serial-contract COMMAND usb_serial_contract_tests
    -o -,txt -o usb-serial-contract-tests.xml,junitxml)
  set_tests_properties(usb-serial-contract PROPERTIES LABELS coverage TIMEOUT 60)
  qt_add_executable(battery_contract_tests ../../tests/desktop/battery_contract_test.cpp)
  target_include_directories(battery_contract_tests PRIVATE . ../../core/src)
  target_compile_features(battery_contract_tests PRIVATE cxx_std_23)
  target_compile_definitions(battery_contract_tests PRIVATE NOMINMAX QT_FORCE_ASSERTS)
  target_link_libraries(battery_contract_tests PRIVATE Qt6::Core Qt6::Test user32)
  qt_import_plugins(battery_contract_tests NO_DEFAULT)
  if(MSVC)
    target_compile_options(battery_contract_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  ap_enable_coverage(battery_contract_tests)
  add_test(NAME battery-contract COMMAND battery_contract_tests
    -o -,txt -o battery-contract-tests.xml,junitxml)
  set_tests_properties(battery-contract PROPERTIES LABELS coverage TIMEOUT 30)
  qt_add_executable(service_contract_tests ../../tests/desktop/service_contract_test.cpp)
  target_include_directories(service_contract_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/identity")
  target_link_libraries(service_contract_tests PRIVATE monitor_backend Qt6::Test)
  target_compile_definitions(service_contract_tests PRIVATE QT_FORCE_ASSERTS)
  qt_import_plugins(service_contract_tests NO_DEFAULT INCLUDE Qt6::QSQLiteDriverPlugin)
  if(MSVC)
    target_compile_options(service_contract_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  ap_enable_coverage(service_contract_tests)
  add_test(NAME service-contract COMMAND service_contract_tests
    -o -,txt -o service-contract-tests.xml,junitxml)
  set_tests_properties(service-contract PROPERTIES LABELS coverage TIMEOUT 120)
  qt_add_executable(cli_contract_tests ../../tests/desktop/cli_contract_test.cpp)
  target_sources(cli_contract_tests PRIVATE ${notice_sources})
  target_include_directories(cli_contract_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/identity")
  target_link_libraries(cli_contract_tests PRIVATE monitor_lib Qt6::Test)
  target_compile_definitions(cli_contract_tests PRIVATE QT_FORCE_ASSERTS)
  if(MSVC)
    target_compile_options(cli_contract_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  # Keep the actual five/ten-second CLI deadlines, but schedule independent
  # scenarios separately instead of serializing every wait in one process.
  set(cli_contract_shards)
  foreach(shard main usb station-failure history)
    set(test_name cli-contract)
    set(test_functions)
    if(NOT shard STREQUAL main)
      string(APPEND test_name "-${shard}")
      set(test_functions commands)
    endif()
    add_test(NAME ${test_name} COMMAND cli_contract_tests ${test_functions} -platform offscreen
      -o -,txt -o "${test_name}-tests.xml,junitxml")
    set_tests_properties(${test_name} PROPERTIES LABELS coverage TIMEOUT 120
      ENVIRONMENT "ALLPOWERS_TEST_CLI_SHARD=${shard}")
    list(APPEND cli_contract_shards ${test_name})
  endforeach()
  # Seed CTest's scheduling estimates on a fresh build, before it has measured
  # these waits. These are priorities only; the test deadlines stay unchanged.
  set_tests_properties(cli-contract PROPERTIES COST 9)
  set_tests_properties(cli-contract-usb cli-contract-station-failure PROPERTIES COST 10)
  set_tests_properties(cli-contract-history PROPERTIES COST 12)
  qt_add_executable(ipc_contract_tests ../../tests/desktop/ipc_contract_test.cpp)
  target_link_libraries(ipc_contract_tests PRIVATE monitor_backend Qt6::Test)
  qt_import_plugins(ipc_contract_tests NO_DEFAULT INCLUDE Qt6::QSQLiteDriverPlugin)
  if(MSVC)
    target_compile_options(ipc_contract_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  ap_enable_coverage(ipc_contract_tests)
  add_test(NAME ipc-contract COMMAND ipc_contract_tests
    -o -,txt -o ipc-contract-tests.xml,junitxml)
  set_tests_properties(ipc-contract PROPERTIES LABELS coverage TIMEOUT 60)
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
  qt_add_executable(language_tests ../../tests/desktop/i18n_test.cpp)
  target_link_libraries(language_tests PRIVATE widget_lib Qt6::Test)
  target_compile_definitions(language_tests PRIVATE
    TRANSLATION_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/translations")
  if(MSVC)
    target_compile_options(language_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  add_test(NAME languages COMMAND language_tests -platform offscreen
    -o -,txt -o languages-tests.xml,junitxml)
  add_test(NAME translation-coverage COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/translation-coverage"
    "-DLUPDATE=$<TARGET_FILE:Qt6::lupdate>" "-DLRELEASE=$<TARGET_FILE:Qt6::lrelease>"
    -P "${ALLPOWERS_ROOT}/cmake/CheckTranslations.cmake")
  set_tests_properties(languages PROPERTIES TIMEOUT 60)
  if(MSVC)
    target_compile_options(widget_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  add_test(NAME widget COMMAND widget_tests -platform offscreen
    -v1 -o -,txt -o widget-tests.xml,junitxml)
  add_test(NAME widget-native COMMAND widget_tests nativeHostRecovery nativeCardPaintAndDrag -platform windows
    -o -,txt -o widget-native-tests.xml,junitxml)

  if(MSVC)
    target_compile_options(monitor_tests PRIVATE /utf-8 /UNDEBUG)
  endif()
  add_test(NAME desktop COMMAND monitor_tests -platform offscreen
    -v1 -o -,txt -o desktop-tests.xml,junitxml)
  add_test(NAME dropdown-native COMMAND monitor_tests dropdownFitsVisibleRows:2-0 dropdownFitsVisibleRows:2-1
    -platform windows -o -,txt -o dropdown-native-tests.xml,junitxml)
  set_tests_properties(dropdown-native PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=1.5")
  # With CTest's redirected handles, Qt otherwise sends text diagnostics to
  # OutputDebugString instead of the captured stream, even with -o -,txt.
  set_property(TEST integration-contract usb-serial-contract battery-contract
    service-contract ${cli_contract_shards} ipc-contract archive-transport languages
    widget widget-native desktop dropdown-native APPEND PROPERTY ENVIRONMENT
    "QT_FORCE_STDERR_LOGGING=1")
  set_tests_properties(widget-native dropdown-native PROPERTIES RESOURCE_LOCK windows-desktop)
  set_tests_properties(history-operation-process archive-transport languages widget widget-native
    desktop dropdown-native PROPERTIES LABELS coverage)
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
if(BUILD_TESTING)
  list(APPEND i18n_targets monitor_tests widget_tests archive_transport_tests language_tests cli_contract_tests)
endif()
foreach(target IN LISTS i18n_targets)
  qt_add_resources(${target} "${target}_translations" PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${allpowers_qm_files})
endforeach()
set(plugin_targets AllpowersMonitor AllpowersWidget)
if(BUILD_TESTING)
  list(APPEND plugin_targets monitor_tests widget_tests archive_transport_tests language_tests cli_contract_tests)
endif()
foreach(target ${plugin_targets})
  qt_import_plugins(${target} NO_DEFAULT INCLUDE
    Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin
    Qt6::QSQLiteDriverPlugin Qt6::QICOPlugin)
endforeach()
if(ALLPOWERS_OPTIMIZE_SIZE AND NOT ALLPOWERS_COVERAGE)
  foreach(target monitor_backend monitor_lib widget_lib protocol ${plugin_targets})
    ap_optimize_size(${target})
  endforeach()
endif()
foreach(target monitor_backend monitor_lib widget_lib protocol ${plugin_targets})
  ap_enable_coverage(${target})
endforeach()
if(BUILD_TESTING)
  ap_enable_coverage(history_operation_id_probe)
endif()
install(TARGETS AllpowersMonitor AllpowersWidget RUNTIME DESTINATION .)
foreach(target monitor_backend monitor_lib widget_lib protocol AllpowersMonitor AllpowersWidget)
  ap_enable_static_analysis(${target})
endforeach()
