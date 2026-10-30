# Contract tests all run in isolated build directories. Keep each script explicit:
# test names are user-facing and need not match filenames or workspace names.
function(ap_add_build_contract)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "NAME;SCRIPT;WORK" "ARGS")
  ap_require_arguments(PREFIX AP REQUIRED NAME SCRIPT)
  if(NOT AP_WORK)
    set(AP_WORK "${AP_NAME}-tests")
  endif()
  add_test(NAME "${AP_NAME}" COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/${AP_WORK}"
    "-DCXX=${CMAKE_CXX_COMPILER}" "-DNINJA=${CMAKE_MAKE_PROGRAM}" ${AP_ARGS}
    -P "${PROJECT_SOURCE_DIR}/${AP_SCRIPT}")
  set_tests_properties("${AP_NAME}" PROPERTIES LABELS build-contract)
endfunction()

ap_add_build_contract(NAME git-hooks SCRIPT tests/cmake/git_hooks_test.cmake WORK git-hook-tests)
ap_add_build_contract(NAME license-notices SCRIPT tests/cmake/licenses_test.cmake WORK license-tests)
ap_add_build_contract(NAME repository-contracts SCRIPT cmake/scripts/quality/CheckContracts.cmake)
ap_add_build_contract(NAME publication-guards SCRIPT tests/cmake/publication_test.cmake WORK publication-tests)
ap_add_build_contract(NAME incremental-build SCRIPT tests/cmake/incremental_test.cmake WORK incremental-test)
ap_add_build_contract(NAME build-identity SCRIPT tests/cmake/build_identity_test.cmake)
ap_add_build_contract(NAME firmware-staging SCRIPT tests/cmake/firmware_staging_test.cmake)
ap_add_build_contract(NAME firmware-graph SCRIPT tests/cmake/firmware_graph_test.cmake)
ap_add_build_contract(NAME partitions SCRIPT tests/cmake/partitions_test.cmake)
ap_add_build_contract(NAME dependency-fingerprint SCRIPT tests/cmake/dependency_fingerprint_test.cmake)
ap_add_build_contract(NAME release-publish SCRIPT tests/cmake/release_publish_test.cmake)
ap_add_build_contract(NAME infrastructure SCRIPT tests/cmake/infrastructure_test.cmake)
ap_add_build_contract(NAME module-boundaries SCRIPT tests/cmake/module_boundaries_test.cmake)
ap_add_build_contract(NAME sdk-tools SCRIPT tests/cmake/sdk_tools_test.cmake)

if(MSVC OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
  ap_add_build_contract(NAME coverage-policy SCRIPT tests/cmake/coverage_policy_test.cmake)
endif()
ap_add_build_contract(NAME release-tags SCRIPT tests/cmake/release_tag_test.cmake WORK release-tag-tests)

if(WIN32 AND MSVC)
  if(ALLPOWERS_STATIC_ANALYSIS)
    ap_add_build_contract(NAME static-analysis-controls SCRIPT tests/cmake/static_analysis_test.cmake
      WORK static-analysis-controls ARGS "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}")
  endif()
  ap_add_build_contract(NAME upx-staging SCRIPT tests/cmake/upx_staging_test.cmake)
  ap_add_build_contract(NAME size-optimization SCRIPT tests/cmake/size_optimization_test.cmake)
  ap_add_build_contract(NAME qt-cache SCRIPT tests/cmake/qt_cache_test.cmake)
  ap_add_build_contract(NAME release-assets SCRIPT tests/cmake/release_assets_test.cmake WORK release-asset-tests)
endif()

# Product and coverage tests keep their own labels and platform requirements.
if(ALLPOWERS_BUILD_WINDOWS)
  include("${CMAKE_CURRENT_LIST_DIR}/Windows.cmake")
endif()
ap_add_coverage_test(ROOT "${PROJECT_SOURCE_DIR}" BUILD_ROOT "${PROJECT_BINARY_DIR}")
get_property(local_checks DIRECTORY PROPERTY TESTS)
set_tests_properties(${local_checks} PROPERTIES WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
