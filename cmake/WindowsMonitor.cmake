include("${CMAKE_CURRENT_LIST_DIR}/QtStatic.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/UpsDriver.cmake")
ap_dependency_fingerprint(qt-sources notice_dependencies)
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/notice-dependencies.txt" CONTENT "${notice_dependencies}\n")
set(windows_build "${PROJECT_BINARY_DIR}/windows-monitor")
set(windows_output "${ALLPOWERS_RELEASE_DIR}/windows/AllpowersMonitor")
set(windows_test_options)
if(BUILD_TESTING)
  list(APPEND windows_test_options TEST_AFTER_INSTALL TRUE TEST_EXCLUDE_FROM_MAIN TRUE
    STEP_TARGETS test LOG_TEST ON
    TEST_COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure --parallel 2)
endif()
ExternalProject_Add(windows-monitor
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/apps/windows"
  BINARY_DIR "${windows_build}"
  DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
  DEPENDS qt-static ups-driver-project
  CMAKE_GENERATOR Ninja
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=${ALLPOWERS_QT_ROOT}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_TRY_COMPILE_CONFIGURATION=${CMAKE_TRY_COMPILE_CONFIGURATION}"
    "-DALLPOWERS_PARENT_IDENTITY=${ALLPOWERS_IDENTITY_FILE}"
    "-DALLPOWERS_RELEASE_TAG=${ALLPOWERS_RELEASE_TAG}"
    "-DALLPOWERS_RELEASE_COMMIT=${ALLPOWERS_RELEASE_COMMIT}"
    "-DALLPOWERS_QT_SOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}"
    "-DCMAKE_INSTALL_PREFIX=${windows_output}"
    "-DALLPOWERS_DRIVER_PACKAGE_DIR=${ALLPOWERS_DRIVER_PACKAGE_DIR}"
    "-DBUILD_TESTING=${BUILD_TESTING}"
    "-DALLPOWERS_COVERAGE=${ALLPOWERS_COVERAGE}"
    "-DALLPOWERS_STATIC_ANALYSIS=${ALLPOWERS_STATIC_ANALYSIS}"
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ALLPOWERS_BUILD_JOBS}"
  BUILD_ALWAYS TRUE
  BUILD_BYPRODUCTS "${windows_build}/AllpowersMonitor.exe" "${windows_build}/AllpowersWidget.exe"
  INSTALL_BYPRODUCTS "${windows_output}/AllpowersMonitor.exe" "${windows_output}/AllpowersWidget.exe"
  ${windows_test_options}
  LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON
  USES_TERMINAL_BUILD TRUE EXCLUDE_FROM_ALL TRUE)
ExternalProject_Add_Step(windows-monitor prepare-notices
  COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${windows_build}"
    "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" -DFETCH=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/PrepareNotices.cmake"
  DEPENDEES patch DEPENDERS configure
  DEPENDS "${PROJECT_SOURCE_DIR}/cmake/PrepareNotices.cmake" "${PROJECT_BINARY_DIR}/notice-dependencies.txt"
    "${PROJECT_SOURCE_DIR}/cmake/CollectQtNotices.cmake"
    "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    "${PROJECT_SOURCE_DIR}/docs/relinking.md" "${PROJECT_SOURCE_DIR}/core/src/AllpowersModels.LICENSE.txt"
    "${PROJECT_SOURCE_DIR}/docs/windows-runtime-terms.md" "${PROJECT_SOURCE_DIR}/docs/licenses/MSVC-STL.md"
    "${PROJECT_SOURCE_DIR}/docs/licenses/UPX.md"
    "${PROJECT_SOURCE_DIR}/drivers/windows/LICENSE.MS-PL.txt"
  LOG ON)
add_custom_target(windows ALL)
add_dependencies(windows windows-monitor)
if(BUILD_TESTING)
  set(windows_coverage_filter)
  if(ALLPOWERS_COVERAGE)
    set(windows_coverage_filter -L "^coverage$")
  endif()
  add_test(NAME windows-unit COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${windows_build}" --output-on-failure --verbose --stop-on-failure --parallel 2 --no-tests=error ${windows_coverage_filter})
  set_tests_properties(windows-unit PROPERTIES TIMEOUT 900 PROCESSORS 2 LABELS runtime)
  if(ALLPOWERS_COVERAGE)
    set_tests_properties(windows-unit PROPERTIES LABELS coverage TIMEOUT 1800)
  endif()
  add_test(NAME single-exe COMMAND "${CMAKE_COMMAND}"
    "-DEXE=${windows_build}/AllpowersMonitor.exe" "-DWORK=${PROJECT_BINARY_DIR}/single-exe-check"
    "-DIDENTITY_FILE=${ALLPOWERS_IDENTITY_FILE}"
    "-DDRIVER_PACKAGE=${ALLPOWERS_DRIVER_PACKAGE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSingleExe.cmake")
  add_test(NAME widget-single-exe COMMAND "${CMAKE_COMMAND}"
    "-DEXE=${windows_build}/AllpowersWidget.exe" "-DWORK=${PROJECT_BINARY_DIR}/widget-single-exe-check"
    "-DIDENTITY_FILE=${ALLPOWERS_IDENTITY_FILE}" -DWIDGET=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSingleExe.cmake")
  add_test(NAME windows-identity-graph COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/windows-identity-graph" "-DQT_ROOT=${ALLPOWERS_QT_ROOT}"
    "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" "-DDEPS_DIR=${ALLPOWERS_DEPS_DIR}"
    "-DCXX=${CMAKE_CXX_COMPILER}" "-DALLPOWERS_BUILD_JOBS=${ALLPOWERS_BUILD_JOBS}"
    -P "${PROJECT_SOURCE_DIR}/tests/cmake/windows_identity_graph_test.cmake")
  set_tests_properties(windows-identity-graph PROPERTIES TIMEOUT 900 LABELS "integration;build-contract" PROCESSORS 4)
  set_tests_properties(single-exe widget-single-exe PROPERTIES LABELS integration)
endif()
# Other build directories may export different binaries to the same release
# directory. Package this build's own EXEs and their embedded driver payload.
install(PROGRAMS "${windows_build}/AllpowersMonitor.exe" DESTINATION .)
install(PROGRAMS "${windows_build}/AllpowersWidget.exe" DESTINATION .)
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME AllpowersMonitor)
set(CPACK_PACKAGE_VERSION "${ALLPOWERS_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "AllpowersMonitor-${ALLPOWERS_VERSION}-win-x64")
set(CPACK_PACKAGE_DIRECTORY "${ALLPOWERS_RELEASE_DIR}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/check-package-identity.cmake" CONTENT [=[
set(ALLPOWERS_ROOT "@PROJECT_SOURCE_DIR@")
set(ALLPOWERS_RELEASE_TAG "@ALLPOWERS_RELEASE_TAG@")
set(ALLPOWERS_RELEASE_COMMIT "@ALLPOWERS_RELEASE_COMMIT@")
set(EXPECTED_VERSION "@ALLPOWERS_VERSION@")
include("@PROJECT_SOURCE_DIR@/cmake/CheckPackageIdentity.cmake")
]=] @ONLY)
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/pack-windows.cmake" CONTENT [=[
set(ALLPOWERS_ROOT "@PROJECT_SOURCE_DIR@")
set(ALLPOWERS_BUILD_ROOT "@PROJECT_BINARY_DIR@")
set(ALLPOWERS_DEPS_DIR "@ALLPOWERS_DEPS_DIR@")
set(ALLPOWERS_DRIVER_PACKAGE_DIR "@ALLPOWERS_DRIVER_PACKAGE_DIR@")
include("@PROJECT_SOURCE_DIR@/cmake/PackWindows.cmake")
]=] @ONLY)
set(CPACK_PRE_BUILD_SCRIPTS "${PROJECT_BINARY_DIR}/check-package-identity.cmake"
  "${PROJECT_BINARY_DIR}/pack-windows.cmake")
include(CPack)
add_custom_target(source-package
  COMMAND "${CMAKE_COMMAND}" -P "${PROJECT_BINARY_DIR}/check-package-identity.cmake"
  COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/source-package"
    "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" "-DOUTPUT=${ALLPOWERS_RELEASE_DIR}"
    "-DQT_ROOT=${ALLPOWERS_QT_ROOT}"
    "-DVERSION=${ALLPOWERS_VERSION}" -P "${PROJECT_SOURCE_DIR}/cmake/PackageSources.cmake"
  DEPENDS windows-monitor USES_TERMINAL VERBATIM)
add_custom_target(release
  COMMAND "${CMAKE_CPACK_COMMAND}" --config "${PROJECT_BINARY_DIR}/CPackConfig.cmake"
  DEPENDS windows source-package-check USES_TERMINAL VERBATIM)
if(BUILD_TESTING)
  add_dependencies(release windows-monitor-test)
endif()
add_custom_target(source-package-check
  COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/source-kit-check"
    "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DARCHIVE=${ALLPOWERS_RELEASE_DIR}/AllpowersMonitor-${ALLPOWERS_VERSION}-sources-with-qt.zip"
    "-DQT_ROOT=${ALLPOWERS_QT_ROOT}"
    "-DALLPOWERS_BUILD_JOBS=${ALLPOWERS_BUILD_JOBS}"
    "-DVERSION=${ALLPOWERS_VERSION}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckSourcePackage.cmake"
  DEPENDS source-package USES_TERMINAL VERBATIM)
