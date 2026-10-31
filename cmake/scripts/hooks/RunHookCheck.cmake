cmake_minimum_required(VERSION 4.4)
foreach(required ROOT TASK NINJA)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
file(TO_CMAKE_PATH "${ROOT}" ROOT)
include("${ROOT}/cmake/Dependencies.cmake")

# Presets and the check target own each mode's build, tests and failure controls.
# Presets own the mode; hooks override only local tools, directories and budgets.
set(work "${ROOT}/build/hooks/${TASK}")
set(preset native)
set(options -S "${ROOT}" -B "${work}" "-DCMAKE_MAKE_PROGRAM=${NINJA}"
  -DALLPOWERS_BUILD_JOBS=2
  "-DALLPOWERS_DEPS_DIR=${DEPS}" "-DALLPOWERS_RELEASE_DIR=${work}/dist")
set(targets check)
set(run_units FALSE)
if(TASK MATCHES "^(windows-unit|linux-unit)$")
  set(targets core native-tests)
  set(run_units TRUE)
endif()
if(PYTHON)
  list(APPEND options "-DPython3_EXECUTABLE=${PYTHON}")
endif()
if(TASK STREQUAL windows-coverage)
  set(preset coverage-windows)
  list(APPEND options
    "-DALLPOWERS_QT_ROOT=${QT}" "-DALLPOWERS_QT_SOURCE_CACHE=${DEPS}/downloads"
    "-DALLPOWERS_COVERAGE_TOOL=${COVERAGE_TOOL}")
elseif(TASK STREQUAL windows-unit)
  # Only portable tests; no Qt/firmware device builds on commit.
elseif(TASK STREQUAL linux-unit)
  list(APPEND options "-DCMAKE_CXX_COMPILER=g++-${ALLPOWERS_GCC_MAJOR}")
elseif(TASK STREQUAL analysis)
  set(preset static-analysis-linux)
  list(APPEND options "-DALLPOWERS_CLANG_TIDY=${TIDY}"
    "-DALLPOWERS_CPPCHECK_ROOT=${CPPCHECK_ROOT}")
elseif(TASK STREQUAL linux-coverage)
  set(preset coverage-linux)
  list(APPEND options "-DCMAKE_CXX_COMPILER=g++-${ALLPOWERS_GCC_MAJOR}"
    "-DALLPOWERS_COVERAGE_TOOL=${COVERAGE_TOOL}")
elseif(TASK STREQUAL sanitizers)
  set(preset sanitizers-linux)
  list(APPEND options "-DCMAKE_CXX_COMPILER=g++-${ALLPOWERS_GCC_MAJOR}")
elseif(TASK STREQUAL valgrind)
  set(preset valgrind-linux)
  list(APPEND options "-DCMAKE_CXX_COMPILER=g++-${ALLPOWERS_GCC_MAJOR}")
  set(targets check-memcheck)
elseif(TASK STREQUAL mutation)
  set(preset mutation-linux)
  list(APPEND options "-DALLPOWERS_MULL_ROOT=${MULL_ROOT}")
elseif(TASK STREQUAL fuzz)
  set(preset fuzz-linux)
  list(APPEND options -DALLPOWERS_FUZZ_SECONDS=30)
else()
  message(FATAL_ERROR "Unknown hook task: ${TASK}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --preset "${preset}" ${options}
  WORKING_DIRECTORY "${ROOT}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${work}" --target ${targets} --parallel 2
  COMMAND_ERROR_IS_FATAL ANY)
if(run_units)
  # Commit hooks intentionally run only the portable units, not Qt/build contracts.
  execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${work}" -L "^coverage$"
    --parallel 2 --output-on-failure --no-tests=error --output-junit "${work}/hook-tests.xml"
    COMMAND_ERROR_IS_FATAL ANY)
endif()
