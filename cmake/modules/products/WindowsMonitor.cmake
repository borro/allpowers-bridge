include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

function(ap_add_windows_product)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(PROJECT_SOURCE_DIR "${AP_ROOT}")
  set(PROJECT_BINARY_DIR "${AP_BUILD_ROOT}")
  include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../sdk/QtStatic.cmake")
  ap_add_qt_sdk(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}")
  include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/UpsDriver.cmake")
  ap_add_windows_driver(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}")
  ap_dependency_fingerprint(GROUP qt-sources OUTPUT notice_dependencies)
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
    # The driver is checked on every build. Let the child's CMake/Ninja graph
    # decide whether its configure inputs changed instead of rerunning it for
    # every completed driver check. Changed CMAKE_ARGS still configure normally.
    CONFIGURE_HANDLED_BY_BUILD TRUE
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS "${windows_build}/AllpowersMonitor.exe" "${windows_build}/AllpowersWidget.exe"
    INSTALL_BYPRODUCTS "${windows_output}/AllpowersMonitor.exe" "${windows_output}/AllpowersWidget.exe"
    ${windows_test_options}
    LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON
    USES_TERMINAL_BUILD TRUE EXCLUDE_FROM_ALL TRUE)
  ExternalProject_Add_Step(windows-monitor prepare-notices
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${windows_build}"
      "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" -DFETCH=ON
      -P "${PROJECT_SOURCE_DIR}/cmake/scripts/release/PrepareNotices.cmake"
    DEPENDEES patch DEPENDERS configure
    DEPENDS "${PROJECT_SOURCE_DIR}/cmake/scripts/release/PrepareNotices.cmake" "${PROJECT_BINARY_DIR}/notice-dependencies.txt"
      "${PROJECT_SOURCE_DIR}/cmake/modules/release/CollectQtNotices.cmake"
      "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
      "${PROJECT_SOURCE_DIR}/docs/relinking.md" "${PROJECT_SOURCE_DIR}/core/src/AllpowersModels.LICENSE.txt"
      "${PROJECT_SOURCE_DIR}/docs/windows-runtime-terms.md" "${PROJECT_SOURCE_DIR}/docs/licenses/MSVC-STL.md"
      "${PROJECT_SOURCE_DIR}/docs/licenses/UPX.md"
      "${PROJECT_SOURCE_DIR}/drivers/windows/LICENSE.MS-PL.txt"
    LOG ON)
  add_custom_target(windows ALL)
  add_dependencies(windows windows-monitor)
  return(PROPAGATE ALLPOWERS_QT_ROOT)
endfunction()
