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
set(test_command "${CMAKE_CTEST_COMMAND}" --test-dir "${binary}"
  --output-on-failure --no-tests=error --parallel 2 -L "^coverage$")
if(WINDOWS)
  file(TO_NATIVE_PATH "${root}/core" core_sources)
  file(TO_NATIVE_PATH "${root}/firmware/R2500Adapter" firmware_sources)
  file(TO_NATIVE_PATH "${root}/apps/windows" windows_sources)
  file(TO_NATIVE_PATH "${binary}" modules)
  execute_process(COMMAND "${TOOL}" --quiet --cover_children
    --modules "${modules}" --sources "${core_sources}" --sources "${firmware_sources}"
    --sources "${windows_sources}"
    --export_type "cobertura:${report}/raw.xml" --export_type "html:${report}/html"
    -- ${test_command} RESULT_VARIABLE tests_result)
else()
  # Only this isolated instrumented build owns these counters.
  file(GLOB_RECURSE counters "${binary}/*.gcda")
  foreach(counter IN LISTS counters)
    file(REAL_PATH "${counter}" resolved)
    cmake_path(IS_PREFIX binary "${resolved}" NORMALIZE inside)
    if(NOT inside OR IS_SYMLINK "${counter}")
      message(FATAL_ERROR "Coverage counter escapes the build directory: ${counter}")
    endif()
    file(REMOVE "${counter}")
  endforeach()
  execute_process(COMMAND ${test_command} RESULT_VARIABLE tests_result)
  execute_process(COMMAND "${TOOL}" --root "${root}" --gcov-executable "${GCOV}"
    --filter "core/" --filter "firmware/R2500Adapter/"
    --cobertura "${report}/raw.xml" --html-details "${report}/index.html"
    "${binary}" WORKING_DIRECTORY "${root}" COMMAND_ERROR_IS_FATAL ANY)
endif()
set(report_options)
if(NOT tests_result STREQUAL "0")
  list(APPEND report_options --incomplete)
endif()
if(WINDOWS)
  list(APPEND report_options --require-prefix apps/windows/)
endif()
execute_process(COMMAND "${PYTHON}" "${root}/cmake/CoverageReport.py"
  --root "${root}" --input "${report}/raw.xml" --output "${report}/coverage.xml"
  --summary "${report}/summary.md" --require-prefix core/
  --require-prefix firmware/R2500Adapter/ ${report_options}
  COMMAND_ERROR_IS_FATAL ANY)
if(NOT tests_result STREQUAL "0")
  message(FATAL_ERROR "Coverage tests failed (${tests_result}); report is incomplete")
endif()
