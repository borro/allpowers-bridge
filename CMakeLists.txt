cmake_minimum_required(VERSION 4.4)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build: cmake --preset native (or windows)")
endif()
include(cmake/Version.cmake)
project(Allpowers VERSION "${ALLPOWERS_VERSION_NUMERIC}" LANGUAGES CXX)
include(cmake/WindowsCompiler.cmake)
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/product-version.txt" CONTENT "${ALLPOWERS_VERSION}\n")
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(cmake/Dependencies.cmake)
set(ALLPOWERS_RELEASE_DIR "${PROJECT_BINARY_DIR}/dist" CACHE PATH "Release artifacts")
set(ALLPOWERS_DEPS_DIR "${PROJECT_BINARY_DIR}/deps" CACHE PATH "Dependency cache")
option(ALLPOWERS_BUILD_WINDOWS "Build the static Windows BLE monitor" OFF)
add_subdirectory(core)
if(BUILD_TESTING)
  add_subdirectory(tests/native)
  add_test(NAME repository-contracts COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckContracts.cmake")
  add_test(NAME publication-guards COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/publication-tests"
    -P "${PROJECT_SOURCE_DIR}/tests/cmake/publication_test.cmake")
  add_test(NAME incremental-build COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/incremental-test" "-DCXX=${CMAKE_CXX_COMPILER}"
    -P "${PROJECT_SOURCE_DIR}/tests/cmake/incremental_test.cmake")
endif()
include(cmake/Arduino.cmake)
add_custom_target(products)
add_dependencies(products core waveshare-147 pocket-096)
if(BUILD_TESTING)
  add_dependencies(products native-tests)
endif()
if(ALLPOWERS_BUILD_WINDOWS)
  if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The Windows monitor requires MSVC and the Windows SDK")
  endif()
  include(cmake/WindowsMonitor.cmake)
  add_dependencies(products windows)
endif()
add_custom_target(audit COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  -P "${PROJECT_SOURCE_DIR}/cmake/Audit.cmake" VERBATIM)
add_custom_target(audit-worktree COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  -DMODE=worktree -P "${PROJECT_SOURCE_DIR}/cmake/Audit.cmake" VERBATIM)
add_custom_target(audit-history COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  -DMODE=history -P "${PROJECT_SOURCE_DIR}/cmake/Audit.cmake" VERBATIM)
add_custom_target(audit-secrets COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  "-DDEPS_DIR=${ALLPOWERS_DEPS_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/tools/gitleaks/${CMAKE_HOST_SYSTEM_NAME}"
  -P "${PROJECT_SOURCE_DIR}/cmake/CheckSecrets.cmake" VERBATIM)
