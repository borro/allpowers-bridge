include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Download.cmake")

function(ap_setup_coverage)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "WORK;OUTPUT" "")
  ap_require_arguments(PREFIX AP REQUIRED WORK OUTPUT)
  include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../Dependencies.cmake")
  get_filename_component(WORK "${AP_WORK}" ABSOLUTE)
  file(MAKE_DIRECTORY "${WORK}")
  file(LOCK "${WORK}/.setup-coverage.lock" GUARD FUNCTION TIMEOUT 600)
  if(CMAKE_HOST_WIN32)
    set(name "OpenCppCoverageSetup-x64-${ALLPOWERS_OPENCPPCOVERAGE_VERSION}.exe")
    set(archive "${WORK}/${name}")
    set(url "https://github.com/OpenCppCoverage/OpenCppCoverage/releases/download/release-${ALLPOWERS_OPENCPPCOVERAGE_VERSION}/${name}")
    set(expected "${ALLPOWERS_OPENCPPCOVERAGE_SHA256}")
  else()
    set(archive "${WORK}/gcovr-${ALLPOWERS_GCOVR_VERSION}-py3-none-any.whl")
    set(url "${ALLPOWERS_GCOVR_URL}")
    set(expected "${ALLPOWERS_GCOVR_SHA256}")
  endif()
  ap_download_verified(URL "${url}" DESTINATION "${archive}"
    SHA256 "${expected}" MISMATCH FAIL TIMEOUT 180 INACTIVITY_TIMEOUT 30)
  if(CMAKE_HOST_WIN32)
    # Extract the official payload; do not run an installer, request elevation,
    # change PATH/registry or install a machine-wide runtime on a developer's PC.
    set(extractor "${WORK}/innoextract-${ALLPOWERS_INNOEXTRACT_VERSION}-windows.zip")
    ap_download_verified(URL "https://constexpr.org/innoextract/files/innoextract-${ALLPOWERS_INNOEXTRACT_VERSION}-windows.zip" DESTINATION "${extractor}"
    SHA256 "${ALLPOWERS_INNOEXTRACT_SHA256}" MISMATCH FAIL TIMEOUT 180 INACTIVITY_TIMEOUT 30)
    file(ARCHIVE_EXTRACT INPUT "${extractor}" DESTINATION "${WORK}/innoextract")
    execute_process(COMMAND "${WORK}/innoextract/innoextract.exe" --silent
      --extract --output-dir "${WORK}/install" "${archive}"
      TIMEOUT 180 COMMAND_ERROR_IS_FATAL ANY)
    if(NOT EXISTS "${WORK}/install/app/OpenCppCoverage.exe")
      message(FATAL_ERROR "OpenCppCoverage payload is missing")
    endif()
  else()
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" -m venv "${WORK}/venv"
      COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${WORK}/venv/bin/python" -m pip install --disable-pip-version-check "${archive}"
      COMMAND_ERROR_IS_FATAL ANY)
  endif()
  if(CMAKE_HOST_WIN32)
    set(tool "${WORK}/install/app/OpenCppCoverage.exe")
  else()
    set(tool "${WORK}/venv/bin/gcovr")
  endif()
  set(${AP_OUTPUT} "${tool}" PARENT_SCOPE)
endfunction()
