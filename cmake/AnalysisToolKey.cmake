cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
execute_process(COMMAND "g++-${ALLPOWERS_GCC_MAJOR}" -dumpfullversion
  OUTPUT_VARIABLE compiler OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND uname -m OUTPUT_VARIABLE arch OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/SetupAnalysisTools.cmake" recipe)
string(SHA256 key "Linux|${arch}|${compiler}|${ALLPOWERS_CPPCHECK_VERSION}|${ALLPOWERS_CPPCHECK_SHA256}|${recipe}|${ALLPOWERS_CMAKE_VERSION}")
if(DEFINED ENV{GITHUB_OUTPUT})
  file(APPEND "$ENV{GITHUB_OUTPUT}" "key=Linux-${arch}-v1-cppcheck-${key}\n")
endif()
message(STATUS "Cppcheck cache: ${key}")
