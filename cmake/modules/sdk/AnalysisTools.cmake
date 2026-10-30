include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Download.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../common/Files.cmake")

function(ap_setup_analysis_tools)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "WORK;OUTPUT;KIND" "")
  ap_require_arguments(PREFIX AP REQUIRED WORK OUTPUT KIND)
  include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../Dependencies.cmake")
  if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Analysis tools bootstrap requires Linux x64")
  endif()
  if(NOT AP_KIND MATCHES "^(workflow|cppcheck)$")
    message(FATAL_ERROR "Set KIND to workflow or cppcheck")
  endif()
  execute_process(COMMAND uname -m OUTPUT_VARIABLE architecture OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  if(NOT architecture STREQUAL "x86_64")
    message(FATAL_ERROR "Pinned analysis tools require Linux x64")
  endif()
  get_filename_component(WORK "${AP_WORK}" ABSOLUTE)
  file(MAKE_DIRECTORY "${WORK}")
  file(LOCK "${WORK}/.setup-analysis_tools.lock" GUARD FUNCTION TIMEOUT 600)
  if(AP_KIND STREQUAL "workflow")
    ap_download_verified(DESTINATION "${WORK}/actionlint.tar.gz" URL "https://github.com/rhysd/actionlint/releases/download/v${ALLPOWERS_ACTIONLINT_VERSION}/actionlint_${ALLPOWERS_ACTIONLINT_VERSION}_linux_amd64.tar.gz" SHA256 "${ALLPOWERS_ACTIONLINT_SHA256}"
      MISMATCH REPLACE TIMEOUT 300)
    ap_download_verified(DESTINATION "${WORK}/shellcheck.tar.xz" URL "https://github.com/koalaman/shellcheck/releases/download/v${ALLPOWERS_SHELLCHECK_VERSION}/shellcheck-v${ALLPOWERS_SHELLCHECK_VERSION}.linux.x86_64.tar.xz" SHA256 "${ALLPOWERS_SHELLCHECK_SHA256}"
      MISMATCH REPLACE TIMEOUT 300)
    file(ARCHIVE_EXTRACT INPUT "${WORK}/actionlint.tar.gz" DESTINATION "${WORK}/actionlint")
    file(ARCHIVE_EXTRACT INPUT "${WORK}/shellcheck.tar.xz" DESTINATION "${WORK}")
  elseif(AP_KIND STREQUAL "cppcheck")
    ap_download_verified(DESTINATION "${WORK}/cppcheck.tar.gz" URL "https://github.com/cppcheck-opensource/cppcheck/archive/refs/tags/${ALLPOWERS_CPPCHECK_VERSION}.tar.gz" SHA256 "${ALLPOWERS_CPPCHECK_SHA256}"
      MISMATCH REPLACE TIMEOUT 300)
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
    ap_write_changed(PATH "${WORK}/control.cpp" CONTENT "int main() { int *p = nullptr; return *p; }\n")
    execute_process(COMMAND "${WORK}/install/bin/cppcheck" --error-exitcode=2 "${WORK}/control.cpp"
      RESULT_VARIABLE status ERROR_VARIABLE diagnostics OUTPUT_QUIET)
    if(NOT status EQUAL 2 OR NOT diagnostics MATCHES "nullPointer")
      message(FATAL_ERROR "Cppcheck did not detect the control defect: ${diagnostics}")
    endif()
  endif()
  if(AP_KIND STREQUAL "cppcheck")
    set(tool "${WORK}/install/bin/cppcheck")
  else()
    set(tool "${WORK}/actionlint/actionlint;${WORK}/shellcheck-v${ALLPOWERS_SHELLCHECK_VERSION}/shellcheck")
  endif()
  set(${AP_OUTPUT} "${tool}" PARENT_SCOPE)
endfunction()
