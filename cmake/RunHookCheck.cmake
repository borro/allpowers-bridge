cmake_minimum_required(VERSION 4.4)
foreach(required ROOT TASK NINJA)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
file(TO_CMAKE_PATH "${ROOT}" ROOT)
set(work "${ROOT}/build/hooks/${TASK}")
set(options -S "${ROOT}" -B "${work}" -G Ninja "-DCMAKE_MAKE_PROGRAM=${NINJA}"
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DALLPOWERS_BUILD_JOBS=2
  "-DALLPOWERS_DEPS_DIR=${DEPS}" "-DALLPOWERS_RELEASE_DIR=${work}/dist")
set(targets core native-tests)
set(test_options -L "^coverage$")
if(PYTHON)
  list(APPEND options "-DPython3_EXECUTABLE=${PYTHON}")
endif()
if(TASK STREQUAL windows-coverage)
  list(APPEND options -DALLPOWERS_BUILD_WINDOWS=ON -DALLPOWERS_COVERAGE=ON
    "-DALLPOWERS_QT_ROOT=${QT}" "-DALLPOWERS_QT_SOURCE_CACHE=${DEPS}/downloads"
    "-DALLPOWERS_COVERAGE_TOOL=${COVERAGE_TOOL}")
  list(APPEND targets windows)
  set(test_options -R "^coverage-report$")
elseif(TASK STREQUAL windows-unit)
  # Only the five portable tests; no Qt/firmware device builds on commit.
elseif(TASK STREQUAL linux-unit)
  list(APPEND options -DCMAKE_CXX_COMPILER=g++-13)
elseif(TASK STREQUAL analysis)
  list(APPEND options -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
    -DALLPOWERS_STATIC_ANALYSIS=ON "-DALLPOWERS_CLANG_TIDY=${TIDY}"
    "-DALLPOWERS_CPPCHECK_ROOT=${CPPCHECK_ROOT}")
  set(targets static-analysis)
elseif(TASK STREQUAL linux-coverage)
  list(APPEND options -DCMAKE_CXX_COMPILER=g++-13 -DALLPOWERS_COVERAGE=ON
    "-DALLPOWERS_COVERAGE_TOOL=${COVERAGE_TOOL}")
  set(test_options -R "^coverage-report$")
elseif(TASK STREQUAL sanitizers)
  list(APPEND options -DCMAKE_CXX_COMPILER=g++-13 -DALLPOWERS_SANITIZERS=ON)
  set(test_options -L "^runtime$")
elseif(TASK STREQUAL valgrind)
  list(APPEND options -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Debug)
  list(APPEND targets valgrind_control)
elseif(TASK STREQUAL mutation)
  list(APPEND options -DCMAKE_CXX_COMPILER=clang++-18 -DALLPOWERS_MUTATION=ON
    "-DALLPOWERS_MULL_ROOT=${MULL_ROOT}")
  set(targets mutation-report)
elseif(TASK STREQUAL fuzz)
  list(APPEND options -DCMAKE_CXX_COMPILER=clang++-18 -DALLPOWERS_FUZZING=ON
    -DALLPOWERS_FUZZ_SECONDS=30)
  set(targets fuzz-tests)
  set(test_options -L "^fuzz$")
else()
  message(FATAL_ERROR "Unknown hook task: ${TASK}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${options} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${work}" --target ${targets} --parallel 2
  COMMAND_ERROR_IS_FATAL ANY)
if(TASK MATCHES "^(analysis|mutation)$")
  return()
endif()
if(TASK STREQUAL valgrind)
  find_program(VALGRIND valgrind REQUIRED)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DCONTROL=${work}/tests/native/valgrind_control"
    "-DWORK=${work}/policy" -P "${ROOT}/tests/cmake/valgrind_policy_test.cmake"
    COMMAND_ERROR_IS_FATAL ANY)
  list(APPEND test_options -T memcheck --timeout 180
    --overwrite "MemoryCheckCommand=${VALGRIND}"
    --overwrite "MemoryCheckCommandOptions=--tool=memcheck --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,indirect,possible --track-origins=yes --error-exitcode=99 --num-callers=30")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${work}" ${test_options}
  --parallel 2 --output-on-failure --no-tests=error --output-junit "${work}/hook-tests.xml"
  COMMAND_ERROR_IS_FATAL ANY)
