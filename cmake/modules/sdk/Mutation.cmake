include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Download.cmake")

function(ap_setup_mutation)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "WORK;OUTPUT" "")
  ap_require_arguments(PREFIX AP REQUIRED WORK OUTPUT)
  include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../Dependencies.cmake")
  if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Pinned Mull requires Linux amd64")
  endif()
  find_program(DPKG_DEB dpkg-deb REQUIRED)
  execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE arch
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(NOT arch STREQUAL "amd64")
    message(FATAL_ERROR "The pinned Mull package requires amd64")
  endif()
  get_filename_component(WORK "${AP_WORK}" ABSOLUTE)
  file(MAKE_DIRECTORY "${WORK}")
  file(LOCK "${WORK}/.setup-mutation.lock" GUARD FUNCTION TIMEOUT 600)
  set(archive "${WORK}/${ALLPOWERS_MULL_PACKAGE}")
  ap_download_verified(URL "${ALLPOWERS_MULL_URL}" DESTINATION "${archive}"
    SHA256 "${ALLPOWERS_MULL_SHA256}" MISMATCH FAIL TIMEOUT 300 INACTIVITY_TIMEOUT 30)
  # Extract the verified package locally; never execute package maintainer scripts.
  execute_process(COMMAND "${DPKG_DEB}" --extract "${archive}" "${WORK}/install"
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${WORK}/install/usr/bin/mull-runner-${ALLPOWERS_MULL_LLVM_VERSION}" --version
    COMMAND_ERROR_IS_FATAL ANY)
  set(tool "${WORK}/install/usr/bin/mull-runner-${ALLPOWERS_MULL_LLVM_VERSION}")
  set(${AP_OUTPUT} "${tool}" PARENT_SCOPE)
endfunction()
