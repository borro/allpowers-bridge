function(ap_add_desktop_tests)
  if(BUILD_TESTING)
    qt_add_executable(runtime_log_tests ../../tests/desktop/runtime_log_test.cpp)
    target_link_libraries(runtime_log_tests PRIVATE monitor_backend Qt6::Test)
    qt_import_plugins(runtime_log_tests NO_DEFAULT)
    if(MSVC)
      target_compile_options(runtime_log_tests PRIVATE /utf-8)
    endif()
    ap_enable_coverage(TARGET runtime_log_tests)
    add_test(NAME runtime-log COMMAND runtime_log_tests)
    set_tests_properties(runtime-log PROPERTIES LABELS coverage TIMEOUT 30)
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
    ap_enable_coverage(TARGET integration_contract_tests)
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
    ap_enable_coverage(TARGET usb_serial_contract_tests)
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
    ap_enable_coverage(TARGET battery_contract_tests)
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
    ap_enable_coverage(TARGET service_contract_tests)
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
    ap_enable_coverage(TARGET ipc_contract_tests)
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
      -P "${ALLPOWERS_ROOT}/cmake/scripts/quality/CheckTranslations.cmake")
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
      -P "${ALLPOWERS_ROOT}/cmake/scripts/quality/CheckSingleExe.cmake")
    add_test(NAME widget-single-exe COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:AllpowersWidget>"
      "-DIDENTITY_FILE=${ALLPOWERS_IDENTITY_FILE}"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/widget-single-exe-check" -DWIDGET=ON
      -P "${ALLPOWERS_ROOT}/cmake/scripts/quality/CheckSingleExe.cmake")

    # These offline suites normally finish in seconds. A deadlock or modal UI
    # must fail with diagnostics instead of hiding inside the parent CTest job.
    set_tests_properties(archive-transport PROPERTIES TIMEOUT 60)
    set_tests_properties(widget widget-native desktop dropdown-native single-exe widget-single-exe
      PROPERTIES TIMEOUT 120)
  endif()
endfunction()
