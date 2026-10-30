include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

function(ap_add_windows_packaging)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(PROJECT_SOURCE_DIR "${AP_ROOT}")
  set(PROJECT_BINARY_DIR "${AP_BUILD_ROOT}")
  set(windows_build "${PROJECT_BINARY_DIR}/windows-monitor")
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
  configure_file("${PROJECT_SOURCE_DIR}/cmake/templates/release/CheckPackageIdentity.cmake.in"
    "${PROJECT_BINARY_DIR}/check-package-identity.cmake" @ONLY)
  configure_file("${PROJECT_SOURCE_DIR}/cmake/templates/release/PackWindows.cmake.in"
    "${PROJECT_BINARY_DIR}/pack-windows.cmake" @ONLY)
  set(CPACK_PRE_BUILD_SCRIPTS "${PROJECT_BINARY_DIR}/check-package-identity.cmake"
    "${PROJECT_BINARY_DIR}/pack-windows.cmake")
  include(CPack)
  add_custom_target(source-package
    COMMAND "${CMAKE_COMMAND}" -P "${PROJECT_BINARY_DIR}/check-package-identity.cmake"
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/source-package"
      "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
      "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" "-DOUTPUT=${ALLPOWERS_RELEASE_DIR}"
      "-DQT_ROOT=${ALLPOWERS_QT_ROOT}"
      "-DVERSION=${ALLPOWERS_VERSION}" -P "${PROJECT_SOURCE_DIR}/cmake/scripts/release/PackageSources.cmake"
    DEPENDS windows-monitor USES_TERMINAL VERBATIM)
  add_custom_target(release
    COMMAND "${CMAKE_CPACK_COMMAND}" --config "${PROJECT_BINARY_DIR}/CPackConfig.cmake"
    DEPENDS windows source-package-check USES_TERMINAL VERBATIM)
  if(BUILD_TESTING AND NOT ALLPOWERS_VERIFY)
    # verify already runs the full Qt suite through check-all before packaging.
    add_dependencies(release windows-monitor-test)
  endif()
  add_custom_target(source-package-check
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/source-kit-check"
      "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
      "-DARCHIVE=${ALLPOWERS_RELEASE_DIR}/AllpowersMonitor-${ALLPOWERS_VERSION}-sources-with-qt.zip"
      "-DQT_ROOT=${ALLPOWERS_QT_ROOT}"
      "-DALLPOWERS_BUILD_JOBS=${ALLPOWERS_BUILD_JOBS}"
      "-DVERSION=${ALLPOWERS_VERSION}" -P "${PROJECT_SOURCE_DIR}/cmake/scripts/release/CheckSourcePackage.cmake"
    DEPENDS source-package USES_TERMINAL VERBATIM)
endfunction()
