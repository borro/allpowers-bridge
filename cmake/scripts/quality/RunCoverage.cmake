cmake_minimum_required(VERSION 4.4)
foreach(required ROOT BINARY TOOL PYTHON)
  if(NOT ${required})
    message(FATAL_ERROR "Missing ${required}")
  endif()
endforeach()
file(REAL_PATH "${ROOT}" root)
file(REAL_PATH "${BINARY}" binary)
set(build_root "${root}/build")
cmake_path(IS_PREFIX build_root "${binary}" NORMALIZE inside)
if(NOT inside OR binary STREQUAL build_root)
  message(FATAL_ERROR "Coverage requires a dedicated directory below the project's build/")
endif()
set(report "${binary}/coverage")
file(LOCK "${binary}/coverage.lock" GUARD PROCESS TIMEOUT 0)
if(IS_SYMLINK "${report}")
  message(FATAL_ERROR "Coverage output must not be a symlink")
endif()
file(REMOVE_RECURSE "${report}")
file(MAKE_DIRECTORY "${report}")
set(test_jobs 2)
if(WINDOWS)
  set(test_jobs 4)
endif()
set(test_command "${CMAKE_CTEST_COMMAND}" --test-dir "${binary}"
  --output-on-failure --no-tests=error --parallel "${test_jobs}" -L "^coverage$"
  --output-junit "${report}/tests.xml")
if(WINDOWS)
  file(TO_NATIVE_PATH "${root}/core" core_sources)
  file(TO_NATIVE_PATH "${root}/apps/windows" windows_sources)
  file(TO_NATIVE_PATH "${binary}" modules)
  execute_process(COMMAND "${TOOL}" --quiet --cover_children
    --modules "${modules}" --sources "${core_sources}"
    --sources "${windows_sources}"
    --export_type "cobertura:${report}/raw.xml" --export_type "html:${report}/html"
    -- ${test_command} RESULT_VARIABLE tests_result)
else()
  # Only this isolated instrumented build owns these counters.
  include("${CMAKE_CURRENT_LIST_DIR}/../../modules/quality/Coverage.cmake")
  ap_collect_coverage_counters(BINARY "${binary}" OUTPUT counters)
  foreach(counter IN LISTS counters)
    file(REAL_PATH "${counter}" resolved)
    cmake_path(IS_PREFIX binary "${resolved}" NORMALIZE inside)
    if(NOT inside OR IS_SYMLINK "${counter}")
      message(FATAL_ERROR "Coverage counter escapes the build directory: ${counter}")
    endif()
    file(REMOVE "${counter}")
  endforeach()
  execute_process(COMMAND ${test_command} RESULT_VARIABLE tests_result)
  # Use the same exclusion during gcovr discovery. Escape arbitrary checkout
  # names, and keep the leading slash so gcovr treats this as an absolute path.
  string(REGEX REPLACE "([][+.*()^$?|\\\\])" "\\\\\\1" tool_cache_regex "${binary}/tools")
  execute_process(COMMAND "${TOOL}" --root "${root}" --gcov-executable "${GCOV}"
    --filter "core/" --filter "firmware/R2500Adapter/"
    --exclude-directory "${tool_cache_regex}(/|$)"
    --cobertura "${report}/raw.xml" --html-details "${report}/index.html"
    "${binary}" WORKING_DIRECTORY "${root}" COMMAND_ERROR_IS_FATAL ANY)
endif()
set(report_options)
if(NOT tests_result STREQUAL "0")
  list(APPEND report_options --incomplete)
endif()
if(WINDOWS)
  set(coverage_platform windows)
  list(APPEND report_options --require-prefix apps/windows/)
else()
  set(coverage_platform linux)
  list(APPEND report_options --require-prefix firmware/R2500Adapter/)
endif()
execute_process(COMMAND "${PYTHON}" "${root}/tools/quality/coverage_report.py"
  --platform "${coverage_platform}"
  --root "${root}" --input "${report}/raw.xml" --output "${report}/coverage.xml"
  --summary "${report}/summary.md" --require-prefix core/
  ${report_options}
  COMMAND_ERROR_IS_FATAL ANY)
if(NOT tests_result STREQUAL "0")
  message(FATAL_ERROR "Coverage tests failed (${tests_result}); report is incomplete")
endif()
