if(BUILD_TESTING)
  add_test(NAME git-hooks COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/git-hook-tests" "-DNINJA=${CMAKE_MAKE_PROGRAM}"
    -P "${PROJECT_SOURCE_DIR}/tests/cmake/git_hooks_test.cmake")
  add_test(NAME license-notices COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/license-tests" -P "${PROJECT_SOURCE_DIR}/tests/cmake/licenses_test.cmake")
  add_test(NAME repository-contracts COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" -P "${PROJECT_SOURCE_DIR}/cmake/scripts/quality/CheckContracts.cmake")
  add_test(NAME publication-guards COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/publication-tests"
    -P "${PROJECT_SOURCE_DIR}/tests/cmake/publication_test.cmake")
  add_test(NAME incremental-build COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/incremental-test" "-DCXX=${CMAKE_CXX_COMPILER}"
    -P "${PROJECT_SOURCE_DIR}/tests/cmake/incremental_test.cmake")
  set(build_checks build-identity firmware-staging firmware-graph partitions dependency-fingerprint
    release-publish infrastructure module-boundaries sdk-tools)
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
if(ALLPOWERS_BUILD_WINDOWS)
  include("${CMAKE_CURRENT_LIST_DIR}/Windows.cmake")
endif()
ap_add_coverage_test(ROOT "${PROJECT_SOURCE_DIR}" BUILD_ROOT "${PROJECT_BINARY_DIR}")
get_property(local_checks DIRECTORY PROPERTY TESTS)
set_tests_properties(${local_checks} PROPERTIES WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
