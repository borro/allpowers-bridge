include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

# Tool caches contain dependencies, not instrumented product targets. Avoid
# walking the entire Python installation just to find this build's counters.
function(ap_collect_coverage_counters)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "BINARY;OUTPUT" "")
  ap_require_arguments(PREFIX AP REQUIRED BINARY OUTPUT)
  set(binary "${AP_BINARY}")
  set(output "${AP_OUTPUT}")
  file(GLOB entries LIST_DIRECTORIES TRUE "${binary}/*")
  set(counters)
  foreach(entry IN LISTS entries)
    if(entry STREQUAL "${binary}/tools")
      continue()
    endif()
    if(IS_DIRECTORY "${entry}")
      # Match GLOB_RECURSE's default: do not follow directory symlinks.
      if(NOT IS_SYMLINK "${entry}")
        file(GLOB_RECURSE nested LIST_DIRECTORIES FALSE "${entry}/*.gcda")
        list(APPEND counters ${nested})
      endif()
    elseif(entry MATCHES "[.]gcda$")
      list(APPEND counters "${entry}")
    endif()
  endforeach()
  set(${output} "${counters}" PARENT_SCOPE)
endfunction()

# Explicit targets only: do not instrument Qt, the driver, SDKs or firmware.
function(ap_enable_coverage)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "TARGET" "")
  ap_require_arguments(PREFIX AP REQUIRED TARGET)
  set(target "${AP_TARGET}")
  if(NOT ALLPOWERS_COVERAGE)
    return()
  endif()
  set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF
    INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
  get_target_property(kind ${target} TYPE)
  if(MSVC)
    # Release CRT matches the existing static Qt SDK; debug information does not
    # require the Debug CRT. Disable folding so distinct lines stay observable.
    target_compile_options(${target} PRIVATE /Od /Ob0 /Zi /UNDEBUG)
    if(NOT kind STREQUAL "STATIC_LIBRARY")
      target_link_options(${target} PRIVATE /DEBUG:FULL /OPT:NOREF /OPT:NOICF /INCREMENTAL:NO)
    endif()
  else()
    target_compile_options(${target} PRIVATE --coverage -O0 -g -fprofile-abs-path)
    target_link_options(${target} PRIVATE --coverage)
  endif()
endfunction()

function(ap_add_coverage_test)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(PROJECT_SOURCE_DIR "${AP_ROOT}")
  set(PROJECT_BINARY_DIR "${AP_BUILD_ROOT}")
  if(NOT ALLPOWERS_COVERAGE)
    return()
  endif()
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
  if(MSVC)
    find_program(ALLPOWERS_COVERAGE_TOOL NAMES OpenCppCoverage
      HINTS "${PROJECT_BINARY_DIR}/tools/coverage/install/app" REQUIRED)
  else()
    find_program(ALLPOWERS_COVERAGE_TOOL NAMES gcovr
      HINTS "${PROJECT_BINARY_DIR}/tools/coverage/venv/bin" REQUIRED)
    # Match versioned/non-default GCC installations instead of silently using
    # the system gcov with an incompatible .gcno format.
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-prog-name=gcov
      OUTPUT_VARIABLE compiler_gcov OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    find_program(ALLPOWERS_GCOV NAMES "${compiler_gcov}" REQUIRED)
  endif()
  add_test(NAME coverage-report COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${PROJECT_SOURCE_DIR}" "-DBINARY=${PROJECT_BINARY_DIR}"
    "-DTOOL=${ALLPOWERS_COVERAGE_TOOL}" "-DGCOV=${ALLPOWERS_GCOV}"
    "-DPYTHON=${Python3_EXECUTABLE}" "-DWINDOWS=${MSVC}"
    -P "${PROJECT_SOURCE_DIR}/cmake/scripts/quality/RunCoverage.cmake")
  set_tests_properties(coverage-report PROPERTIES TIMEOUT 2400 RUN_SERIAL TRUE)
endfunction()
