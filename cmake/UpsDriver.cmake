include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/BuildParallelism.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DependencyFingerprint.cmake")
ap_dependency_fingerprint(wdk wdk_dependencies)
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/wdk-dependencies.txt" CONTENT "${wdk_dependencies}\n")
ExternalProject_Add(ups-driver-project
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/drivers/windows"
  BINARY_DIR "${PROJECT_BINARY_DIR}/ups-driver"
  DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
  CMAKE_GENERATOR Ninja CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_TRY_COMPILE_CONFIGURATION=${CMAKE_TRY_COMPILE_CONFIGURATION}"
    "-DALLPOWERS_DEPS_DIR=${ALLPOWERS_DEPS_DIR}"
    "-DALLPOWERS_RELEASE_TAG=${ALLPOWERS_RELEASE_TAG}"
    "-DALLPOWERS_RELEASE_COMMIT=${ALLPOWERS_RELEASE_COMMIT}"
    "-DALLPOWERS_PARENT_IDENTITY=${ALLPOWERS_IDENTITY_FILE}"
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ALLPOWERS_BUILD_JOBS}"
  BUILD_ALWAYS TRUE INSTALL_COMMAND "" EXCLUDE_FROM_ALL TRUE
  LOG_CONFIGURE ON LOG_BUILD ON LOG_OUTPUT_ON_FAILURE ON)
add_dependencies(ups-driver-project product-identity)
ExternalProject_Add_Step(ups-driver-project prepare-sdk
  COMMAND "${CMAKE_COMMAND}" "-DDEPS_DIR=${ALLPOWERS_DEPS_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/SetupWdk.cmake"
  DEPENDEES patch DEPENDERS configure
  DEPENDS "${PROJECT_SOURCE_DIR}/cmake/SetupWdk.cmake" "${PROJECT_BINARY_DIR}/wdk-dependencies.txt"
  BYPRODUCTS "${ALLPOWERS_DEPS_DIR}/wdk/${ALLPOWERS_WDK_VERSION}/.complete"
    "${ALLPOWERS_DEPS_DIR}/wdk/${ALLPOWERS_WDK_VERSION}/c/Include/wdf/kmdf/${ALLPOWERS_KMDF_VERSION}/wdf.h"
  LOG ON)
add_custom_target(ups-driver)
add_dependencies(ups-driver ups-driver-project)
set(ALLPOWERS_DRIVER_PACKAGE_DIR "${PROJECT_BINARY_DIR}/ups-driver/package" CACHE PATH
  "UPS driver payload; VM-signed replacement may be selected for lab builds")
