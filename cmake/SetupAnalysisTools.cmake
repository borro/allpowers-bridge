cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR NOT WORK)
  message(FATAL_ERROR "Analysis tools bootstrap requires Linux x64 and -DWORK=<tool directory>")
endif()
execute_process(COMMAND uname -m OUTPUT_VARIABLE architecture OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
if(NOT architecture STREQUAL "x86_64")
  message(FATAL_ERROR "Pinned analysis tools require Linux x64")
endif()
get_filename_component(WORK "${WORK}" ABSOLUTE)
file(MAKE_DIRECTORY "${WORK}")
function(download name url hash)
  set(path "${WORK}/${name}")
  if(EXISTS "${path}")
    file(SHA256 "${path}" actual)
  endif()
  if(NOT actual STREQUAL hash)
    file(DOWNLOAD "${url}" "${path}" EXPECTED_HASH "SHA256=${hash}"
      TLS_VERIFY ON TIMEOUT 300 INACTIVITY_TIMEOUT 30)
  endif()
endfunction()
if(KIND STREQUAL "workflow")
  download(actionlint.tar.gz
    "https://github.com/rhysd/actionlint/releases/download/v${ALLPOWERS_ACTIONLINT_VERSION}/actionlint_${ALLPOWERS_ACTIONLINT_VERSION}_linux_amd64.tar.gz"
    "${ALLPOWERS_ACTIONLINT_SHA256}")
  download(shellcheck.tar.xz
    "https://github.com/koalaman/shellcheck/releases/download/v${ALLPOWERS_SHELLCHECK_VERSION}/shellcheck-v${ALLPOWERS_SHELLCHECK_VERSION}.linux.x86_64.tar.xz"
    "${ALLPOWERS_SHELLCHECK_SHA256}")
  file(ARCHIVE_EXTRACT INPUT "${WORK}/actionlint.tar.gz" DESTINATION "${WORK}/actionlint")
  file(ARCHIVE_EXTRACT INPUT "${WORK}/shellcheck.tar.xz" DESTINATION "${WORK}")
elseif(KIND STREQUAL "cppcheck")
  download(cppcheck.tar.gz
    "https://github.com/cppcheck-opensource/cppcheck/archive/refs/tags/${ALLPOWERS_CPPCHECK_VERSION}.tar.gz"
    "${ALLPOWERS_CPPCHECK_SHA256}")
  set(source "${WORK}/cppcheck-${ALLPOWERS_CPPCHECK_VERSION}")
  # Cache only this tool, never product objects or previous analysis results.
  if(NOT EXISTS "${WORK}/install/bin/cppcheck")
    file(ARCHIVE_EXTRACT INPUT "${WORK}/cppcheck.tar.gz" DESTINATION "${WORK}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${WORK}/build" -G Ninja
      -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=${WORK}/install"
      "-DCMAKE_CXX_COMPILER=g++-${ALLPOWERS_GCC_MAJOR}"
      -DBUILD_GUI=OFF -DBUILD_TESTS=OFF -DUSE_MATCHCOMPILER=ON
      COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}/build" --parallel 4
      COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${CMAKE_COMMAND}" --install "${WORK}/build"
      COMMAND_ERROR_IS_FATAL ANY)
  endif()
  execute_process(COMMAND "${WORK}/install/bin/cppcheck" --version
    OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(NOT version STREQUAL "Cppcheck ${ALLPOWERS_CPPCHECK_VERSION}")
    message(FATAL_ERROR "Unexpected cached tool: ${version}")
  endif()
  file(WRITE "${WORK}/control.cpp" "int main() { int *p = nullptr; return *p; }\n")
  execute_process(COMMAND "${WORK}/install/bin/cppcheck" --error-exitcode=2 "${WORK}/control.cpp"
    RESULT_VARIABLE status ERROR_VARIABLE diagnostics OUTPUT_QUIET)
  if(NOT status EQUAL 2 OR NOT diagnostics MATCHES "nullPointer")
    message(FATAL_ERROR "Cppcheck did not detect the control defect: ${diagnostics}")
  endif()
else()
  message(FATAL_ERROR "Set KIND to workflow or cppcheck")
endif()
