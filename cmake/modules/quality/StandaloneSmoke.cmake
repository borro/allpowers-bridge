include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

function(ap_run_smoke)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "LANGUAGE;REPORT_PATH;WORK;EXE_NAME;TIMEOUT" "")
  ap_require_arguments(PREFIX AP REQUIRED LANGUAGE REPORT_PATH WORK EXE_NAME)
  if(NOT DEFINED AP_TIMEOUT)
    # This includes process/Qt startup, before the application's separate
    # ten-second readiness deadline starts. Cold CI launches need that headroom.
    set(AP_TIMEOUT 60)
  endif()
  if(NOT AP_TIMEOUT MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "Standalone process timeout must be a positive integer")
  endif()
  set(language_args)
  if(NOT AP_LANGUAGE STREQUAL "system")
    set(language_args --language "${AP_LANGUAGE}")
  endif()
  # Never mistake a report from an earlier run for this process's result.
  file(REMOVE "${AP_REPORT_PATH}")
  string(TIMESTAMP started "%s" UTC)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    --unset=QT_PLUGIN_PATH --unset=QT_QPA_PLATFORM_PLUGIN_PATH --unset=QT_QPA_PLATFORM
    "QT_FORCE_STDERR_LOGGING=1" "PATH=$ENV{SystemRoot}/System32" -- "${AP_WORK}/${AP_EXE_NAME}"
    --self-test ${language_args} --report "${AP_REPORT_PATH}" -platform offscreen
    WORKING_DIRECTORY "${AP_WORK}" TIMEOUT "${AP_TIMEOUT}" RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
  string(TIMESTAMP finished "%s" UTC)
  math(EXPR elapsed "${finished} - ${started}")
  set(timing "language=${AP_LANGUAGE}, elapsed=${elapsed}s, limit=${AP_TIMEOUT}s, result=${result}")
  file(WRITE "${AP_WORK}/self-test-${AP_LANGUAGE}-out.log" "${timing}\n${stdout}")
  file(WRITE "${AP_WORK}/self-test-${AP_LANGUAGE}-err.log" "${stderr}")
  if(NOT result STREQUAL "0")
    set(diagnostics "No JSON report was produced.")
    if(EXISTS "${AP_REPORT_PATH}")
      file(READ "${AP_REPORT_PATH}" diagnostics LIMIT 12000)
    endif()
    message(FATAL_ERROR "${AP_EXE_NAME} standalone self-test failed: ${timing}\n"
      "${stdout}\n${stderr}\n${diagnostics}")
  endif()
  message(STATUS "${AP_EXE_NAME} standalone self-test: ${timing}")
endfunction()
