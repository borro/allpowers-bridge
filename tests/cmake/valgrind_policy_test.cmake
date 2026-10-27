cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/Dependencies.cmake")
if(NOT CONTROL OR NOT WORK)
  message(FATAL_ERROR "Pass -DCONTROL=<valgrind_control> -DWORK=<log directory>")
endif()
find_program(VALGRIND valgrind REQUIRED)
execute_process(COMMAND "${VALGRIND}" --version OUTPUT_VARIABLE version
  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT version STREQUAL "valgrind-${ALLPOWERS_VALGRIND_VERSION}")
  message(FATAL_ERROR "Expected Valgrind ${ALLPOWERS_VALGRIND_VERSION}; got ${version}")
endif()
file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${CONTROL}" OUTPUT_FILE "${WORK}/baseline.log"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${VALGRIND}" --tool=memcheck --track-origins=yes
  --error-exitcode=99 "--log-file=${WORK}/control.log" "${CONTROL}"
  RESULT_VARIABLE result OUTPUT_FILE "${WORK}/stdout.log" TIMEOUT 30)
file(READ "${WORK}/control.log" diagnostics)
if(NOT result STREQUAL "99" OR NOT diagnostics MATCHES "[Uu]ninitialised")
  message(FATAL_ERROR "Memcheck must diagnose the uninitialised read and exit 99: ${result}\n${diagnostics}")
endif()
message(STATUS "${version}: deliberate uninitialised read correctly rejected with exit 99")
