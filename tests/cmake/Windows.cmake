if(BUILD_TESTING)
  set(windows_coverage_filter)
  if(ALLPOWERS_COVERAGE)
    set(windows_coverage_filter -L "^coverage$")
  endif()
  add_test(NAME windows-unit COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}/windows-monitor" --output-on-failure --verbose --stop-on-failure --parallel 4 --no-tests=error ${windows_coverage_filter})
  set_tests_properties(windows-unit PROPERTIES TIMEOUT 900 PROCESSORS 4 LABELS runtime)
  if(ALLPOWERS_COVERAGE)
    set_tests_properties(windows-unit PROPERTIES LABELS coverage TIMEOUT 1800)
  endif()
  add_test(NAME single-exe COMMAND "${CMAKE_COMMAND}"
    "-DEXE=${PROJECT_BINARY_DIR}/windows-monitor/AllpowersMonitor.exe" "-DWORK=${PROJECT_BINARY_DIR}/single-exe-check"
    "-DIDENTITY_FILE=${ALLPOWERS_IDENTITY_FILE}"
    "-DDRIVER_PACKAGE=${ALLPOWERS_DRIVER_PACKAGE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/scripts/quality/CheckSingleExe.cmake")
  add_test(NAME widget-single-exe COMMAND "${CMAKE_COMMAND}"
    "-DEXE=${PROJECT_BINARY_DIR}/windows-monitor/AllpowersWidget.exe" "-DWORK=${PROJECT_BINARY_DIR}/widget-single-exe-check"
    "-DIDENTITY_FILE=${ALLPOWERS_IDENTITY_FILE}" -DWIDGET=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/scripts/quality/CheckSingleExe.cmake")
  add_test(NAME windows-identity-graph COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/windows-identity-graph" "-DQT_ROOT=${ALLPOWERS_QT_ROOT}"
    "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" "-DDEPS_DIR=${ALLPOWERS_DEPS_DIR}"
    "-DCXX=${CMAKE_CXX_COMPILER}" "-DALLPOWERS_BUILD_JOBS=${ALLPOWERS_BUILD_JOBS}"
    -P "${PROJECT_SOURCE_DIR}/tests/cmake/windows_identity_graph_test.cmake")
  set_tests_properties(windows-identity-graph PROPERTIES TIMEOUT 900 LABELS "integration;build-contract" PROCESSORS 4)
  set_tests_properties(single-exe widget-single-exe PROPERTIES LABELS integration)
endif()
