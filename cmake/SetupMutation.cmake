cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR NOT WORK)
  message(FATAL_ERROR "Run on Ubuntu 24.04 amd64 with -DWORK=<mutation build>/tools/mull")
endif()
find_program(DPKG_DEB dpkg-deb REQUIRED)
execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE arch
  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT arch STREQUAL "amd64")
  message(FATAL_ERROR "The pinned Mull package requires amd64")
endif()
get_filename_component(WORK "${WORK}" ABSOLUTE)
file(MAKE_DIRECTORY "${WORK}")
set(archive "${WORK}/${ALLPOWERS_MULL_PACKAGE}")
if(NOT EXISTS "${archive}")
  file(DOWNLOAD "${ALLPOWERS_MULL_URL}" "${archive}"
    EXPECTED_HASH "SHA256=${ALLPOWERS_MULL_SHA256}" TLS_VERIFY ON
    TIMEOUT 300 INACTIVITY_TIMEOUT 30)
endif()
file(SHA256 "${archive}" actual)
if(NOT actual STREQUAL ALLPOWERS_MULL_SHA256)
  message(FATAL_ERROR "Mull package checksum mismatch")
endif()
# Extract the verified package locally; never execute package maintainer scripts.
execute_process(COMMAND "${DPKG_DEB}" --extract "${archive}" "${WORK}/install"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${WORK}/install/usr/bin/mull-runner-${ALLPOWERS_MULL_LLVM_VERSION}" --version
  COMMAND_ERROR_IS_FATAL ANY)
