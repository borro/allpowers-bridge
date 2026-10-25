cmake_minimum_required(VERSION 3.25)
if(DEFINED BINARY)
  include("${ROOT}/cmake/Partitions.cmake")
  file(READ "${ROOT}/tests/fixtures/usb-contract.json" contract)
  ap_partitions(actual "${BINARY}" "${contract}")
  return()
endif()
if(WIN32)
  set(CMAKE_EXECUTABLE_SUFFIX ".exe")
endif()
find_program(NINJA NAMES ninja REQUIRED)
file(MAKE_DIRECTORY "${WORK}/source")
file(WRITE "${WORK}/source/CMakeLists.txt" "cmake_minimum_required(VERSION 3.25)\nproject(PartitionFixture LANGUAGES CXX)\nadd_executable(partition-fixture \"${ROOT}/tests/cmake/partition_fixture.cpp\")\ntarget_compile_features(partition-fixture PRIVATE cxx_std_17)\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${WORK}/source" -B "${WORK}/build" -G Ninja
  "-DCMAKE_CXX_COMPILER=${CXX}" "-DCMAKE_MAKE_PROGRAM=${NINJA}"
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
  RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(result)
  message(FATAL_ERROR "Fixture configure failed: ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}/build"
  COMMAND_ERROR_IS_FATAL ANY OUTPUT_QUIET)
execute_process(COMMAND "${WORK}/build/partition-fixture${CMAKE_EXECUTABLE_SUFFIX}" "${WORK}/fixtures"
  COMMAND_ERROR_IS_FATAL ANY)
foreach(name valid oversize truncated type subtype flags address size checksum trailing duplicate extra missing)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DROOT=${ROOT}" "-DBINARY=${WORK}/fixtures/${name}.bin"
    -P "${CMAKE_CURRENT_LIST_FILE}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error)
  if((name STREQUAL valid AND result) OR (NOT name STREQUAL valid AND NOT result))
    message(FATAL_ERROR "Wrong partition validation for ${name}: ${out}${error}")
  endif()
endforeach()
