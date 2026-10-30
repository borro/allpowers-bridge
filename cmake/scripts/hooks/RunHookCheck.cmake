cmake_minimum_required(VERSION 4.4)
foreach(required ROOT TASK NINJA)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
file(TO_CMAKE_PATH "${ROOT}" ROOT)
include("${ROOT}/cmake/Dependencies.cmake")

# Describe the task first. All tasks then share configure -> build -> control -> test.
# Presets own the mode; hooks override only local tools, directories and budgets.
set(work "${ROOT}/build/hooks/${TASK}")
set(preset native)
set(options -S "${ROOT}" -B "${work}" "-DCMAKE_MAKE_PROGRAM=${NINJA}"
  -DALLPOWERS_BUILD_JOBS=2
  "-DALLPOWERS_DEPS_DIR=${DEPS}" "-DALLPOWERS_RELEASE_DIR=${work}/dist")
set(targets core native-tests)
set(test_options -L "^coverage$")
set(run_tests TRUE)
set(control_command)
if(PYTHON)
  list(APPEND options "-DPython3_EXECUTABLE=${PYTHON}")
endif()
if(TASK STREQUAL windows-coverage)
  set(preset coverage-windows)
  list(APPEND options
    "-DALLPOWERS_QT_ROOT=${QT}" "-DALLPOWERS_QT_SOURCE_CACHE=${DEPS}/downloads"
    "-DALLPOWERS_COVERAGE_TOOL=${COVERAGE_TOOL}")
  list(APPEND targets windows)
  set(test_options -R "^coverage-report$")
elseif(TASK STREQUAL windows-unit)
  # Only the five portable tests; no Qt/firmware device builds on commit.
elseif(TASK STREQUAL linux-unit)
  list(APPEND options "-DCMAKE_CXX_COMPILER=g++-${ALLPOWERS_GCC_MAJOR}")
elseif(TASK STREQUAL analysis)
  set(preset static-analysis-linux)
  list(APPEND options "-DALLPOWERS_CLANG_TIDY=${TIDY}"
    "-DALLPOWERS_CPPCHECK_ROOT=${CPPCHECK_ROOT}")
  set(targets static-analysis)
  set(run_tests FALSE) # This build target runs the analyzer and its controls.
elseif(TASK STREQUAL linux-coverage)
  set(preset coverage-linux)
  list(APPEND options "-DCMAKE_CXX_COMPILER=g++-${ALLPOWERS_GCC_MAJOR}"
    "-DALLPOWERS_COVERAGE_TOOL=${COVERAGE_TOOL}")
  set(test_options -R "^coverage-report$")
elseif(TASK STREQUAL sanitizers)
  set(preset sanitizers-linux)
  list(APPEND options "-DCMAKE_CXX_COMPILER=g++-${ALLPOWERS_GCC_MAJOR}")
  set(test_options -L "^runtime$")
elseif(TASK STREQUAL valgrind)
  set(preset valgrind-linux)
  list(APPEND options "-DCMAKE_CXX_COMPILER=g++-${ALLPOWERS_GCC_MAJOR}")
  list(APPEND targets valgrind_control)
  find_program(VALGRIND valgrind REQUIRED)
  set(control_command "${CMAKE_COMMAND}" "-DCONTROL=${work}/tests/native/valgrind_control"
    "-DWORK=${work}/policy" -P "${ROOT}/tests/cmake/valgrind_policy_test.cmake")
  list(APPEND test_options -T memcheck --timeout 180
    --overwrite "MemoryCheckCommand=${VALGRIND}"
    --overwrite "MemoryCheckCommandOptions=--tool=memcheck --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,indirect,possible --track-origins=yes --error-exitcode=99 --num-callers=30")
elseif(TASK STREQUAL mutation)
  set(preset mutation-linux)
  list(APPEND options "-DALLPOWERS_MULL_ROOT=${MULL_ROOT}")
  set(targets mutation-report)
  set(run_tests FALSE) # The report target includes the baseline and mutation controls.
elseif(TASK STREQUAL fuzz)
  set(preset fuzz-linux)
  list(APPEND options -DALLPOWERS_FUZZ_SECONDS=30)
  set(targets fuzz-tests)
  set(test_options -L "^fuzz$")
else()
  message(FATAL_ERROR "Unknown hook task: ${TASK}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --preset "${preset}" ${options}
  WORKING_DIRECTORY "${ROOT}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${work}" --target ${targets} --parallel 2
  COMMAND_ERROR_IS_FATAL ANY)
if(control_command)
  execute_process(COMMAND ${control_command} COMMAND_ERROR_IS_FATAL ANY)
endif()
if(run_tests)
  execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${work}" ${test_options}
    --parallel 2 --output-on-failure --no-tests=error --output-junit "${work}/hook-tests.xml"
    COMMAND_ERROR_IS_FATAL ANY)
endif()
