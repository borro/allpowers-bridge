include("${CMAKE_CURRENT_LIST_DIR}/QtStatic.cmake")
set(windows_build "${PROJECT_BINARY_DIR}/windows-monitor")
set(windows_output "${ALLPOWERS_RELEASE_DIR}/windows/AllpowersMonitor")
ExternalProject_Add(windows-monitor
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/apps/windows"
  BINARY_DIR "${windows_build}"
  DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
  DEPENDS qt-static
  CMAKE_GENERATOR Ninja
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=${ALLPOWERS_QT_ROOT}"
    "-DALLPOWERS_VERSION=${ALLPOWERS_VERSION}" "-DALLPOWERS_QT_SOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}"
    "-DCMAKE_INSTALL_PREFIX=${windows_output}"
  BUILD_ALWAYS TRUE
  BUILD_BYPRODUCTS "${windows_output}/AllpowersMonitor.exe"
  LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON
  USES_TERMINAL_BUILD TRUE EXCLUDE_FROM_ALL TRUE)
ExternalProject_Add_Step(windows-monitor verify
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${windows_build}" --output-on-failure
  DEPENDEES build DEPENDERS install ALWAYS TRUE LOG ON)
add_custom_target(windows DEPENDS windows-monitor)
if(BUILD_TESTING)
  add_test(NAME windows-unit COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${windows_build}" --output-on-failure)
  add_test(NAME single-exe COMMAND "${CMAKE_COMMAND}"
    "-DEXE=${windows_output}/AllpowersMonitor.exe" "-DWORK=${PROJECT_BINARY_DIR}/single-exe-check"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckSingleExe.cmake")
endif()
install(PROGRAMS "${windows_output}/AllpowersMonitor.exe" DESTINATION .)
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME AllpowersMonitor)
set(CPACK_PACKAGE_VERSION "${ALLPOWERS_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "AllpowersMonitor-${ALLPOWERS_VERSION}-win-x64")
set(CPACK_PACKAGE_DIRECTORY "${ALLPOWERS_RELEASE_DIR}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
include(CPack)
add_custom_target(source-package
  COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/source-package"
    "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" "-DOUTPUT=${ALLPOWERS_RELEASE_DIR}"
    "-DVERSION=${ALLPOWERS_VERSION}" -P "${PROJECT_SOURCE_DIR}/cmake/PackageSources.cmake"
  DEPENDS windows-monitor USES_TERMINAL VERBATIM)
add_custom_target(release
  COMMAND "${CMAKE_CPACK_COMMAND}" --config "${PROJECT_BINARY_DIR}/CPackConfig.cmake"
  DEPENDS windows source-package-check USES_TERMINAL VERBATIM)
add_custom_target(source-package-check
  COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/source-kit-check"
    "-DARCHIVE=${ALLPOWERS_RELEASE_DIR}/AllpowersMonitor-${ALLPOWERS_VERSION}-sources.zip"
    "-DVERSION=${ALLPOWERS_VERSION}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckSourcePackage.cmake"
  DEPENDS source-package USES_TERMINAL VERBATIM)
