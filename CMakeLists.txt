cmake_minimum_required(VERSION 4.4)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build: cmake --preset native (or windows)")
endif()
include(cmake/Version.cmake)
project(Allpowers VERSION "${ALLPOWERS_VERSION_NUMERIC}" LANGUAGES CXX)
include(cmake/WindowsCompiler.cmake)
ap_add_build_identity()
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(cmake/Dependencies.cmake)
include(cmake/BuildParallelism.cmake)
set(ALLPOWERS_RELEASE_DIR "${PROJECT_BINARY_DIR}/dist" CACHE PATH "Release artifacts")
set(ALLPOWERS_DEPS_DIR "${PROJECT_BINARY_DIR}/deps" CACHE PATH "Dependency cache")
option(ALLPOWERS_BUILD_WINDOWS "Build the static Windows BLE monitor" OFF)
include(cmake/Fuzzing.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Coverage.cmake)
include(cmake/Mutation.cmake)
include(cmake/StaticAnalysis.cmake)
add_subdirectory(core)
add_dependencies(core product-identity)
if(BUILD_TESTING)
  add_subdirectory(tests/native)
  add_test(NAME git-hooks COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/git-hook-tests" "-DNINJA=${CMAKE_MAKE_PROGRAM}"
    -P "${PROJECT_SOURCE_DIR}/tests/cmake/git_hooks_test.cmake")
  if(ALLPOWERS_FUZZING)
    add_subdirectory(tests/fuzz)
  endif()
  add_test(NAME license-notices COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/license-tests" -P "${PROJECT_SOURCE_DIR}/tests/cmake/licenses_test.cmake")
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
  set(build_checks build-identity firmware-staging firmware-graph partitions dependency-fingerprint release-publish)
  if(MSVC OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    list(APPEND build_checks coverage-policy)
  endif()
  foreach(check IN LISTS build_checks)
    string(REPLACE "-" "_" script "${check}")
    add_test(NAME ${check} COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
      "-DBUILD_ROOT=${PROJECT_BINARY_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/${check}-tests"
      "-DCXX=${CMAKE_CXX_COMPILER}" "-DNINJA=${CMAKE_MAKE_PROGRAM}"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/${script}_test.cmake")
  endforeach()
  add_test(NAME release-tags COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/release-tag-tests"
    -P "${PROJECT_SOURCE_DIR}/tests/cmake/release_tag_test.cmake")
  if(WIN32 AND MSVC)
    if(ALLPOWERS_STATIC_ANALYSIS)
      add_test(NAME static-analysis-controls COMMAND "${CMAKE_COMMAND}"
        "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/static-analysis-controls"
        "-DCXX=${CMAKE_CXX_COMPILER}" "-DNINJA=${CMAKE_MAKE_PROGRAM}"
        "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/static_analysis_test.cmake")
    endif()
    add_test(NAME upx-staging COMMAND "${CMAKE_COMMAND}"
      "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/upx-staging-tests"
      "-DNINJA=${CMAKE_MAKE_PROGRAM}"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/upx_staging_test.cmake")
    add_test(NAME size-optimization COMMAND "${CMAKE_COMMAND}"
      "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/size-optimization-tests"
      "-DCXX=${CMAKE_CXX_COMPILER}" "-DNINJA=${CMAKE_MAKE_PROGRAM}"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/size_optimization_test.cmake")
    add_test(NAME qt-cache COMMAND "${CMAKE_COMMAND}"
      "-DROOT=${PROJECT_SOURCE_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/qt-cache-tests"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/qt_cache_test.cmake")
    add_test(NAME release-assets COMMAND "${CMAKE_COMMAND}"
      "-DROOT=${PROJECT_SOURCE_DIR}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
      "-DWORK=${PROJECT_BINARY_DIR}/release-asset-tests" "-DCXX=${CMAKE_CXX_COMPILER}"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/release_assets_test.cmake")
  endif()
endif()
if(BUILD_TESTING)
  get_property(build_contracts DIRECTORY PROPERTY TESTS)
  set_tests_properties(${build_contracts} PROPERTIES LABELS build-contract)
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
ap_add_coverage_test()
ap_add_mutation_target()
ap_add_analysis_targets()
add_custom_target(audit COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  -P "${PROJECT_SOURCE_DIR}/cmake/Audit.cmake" VERBATIM)
add_custom_target(format-check COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  "-DWORK=${PROJECT_BINARY_DIR}/format" -P "${PROJECT_SOURCE_DIR}/cmake/RunFormat.cmake" VERBATIM)
add_custom_target(pre-commit COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  -DPHASE=commit -P "${PROJECT_SOURCE_DIR}/cmake/GitHooks.cmake" USES_TERMINAL VERBATIM)
add_custom_target(pre-push COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  -DPHASE=push -DREVISION=HEAD -P "${PROJECT_SOURCE_DIR}/cmake/GitHooks.cmake" USES_TERMINAL VERBATIM)
add_custom_target(install-git-hooks COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  -P "${PROJECT_SOURCE_DIR}/cmake/InstallGitHooks.cmake" USES_TERMINAL VERBATIM)
add_custom_target(audit-worktree COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  -DMODE=worktree -P "${PROJECT_SOURCE_DIR}/cmake/Audit.cmake" VERBATIM)
add_custom_target(audit-history COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  -DMODE=history -P "${PROJECT_SOURCE_DIR}/cmake/Audit.cmake" VERBATIM)
add_custom_target(audit-secrets COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
  "-DDEPS_DIR=${ALLPOWERS_DEPS_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/tools/gitleaks/${CMAKE_HOST_SYSTEM_NAME}"
  -P "${PROJECT_SOURCE_DIR}/cmake/CheckSecrets.cmake" VERBATIM)
