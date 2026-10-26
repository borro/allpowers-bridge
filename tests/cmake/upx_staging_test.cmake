cmake_minimum_required(VERSION 4.4)
file(MAKE_DIRECTORY "${WORK}/build/_CPack_Packages" "${WORK}/outside")
file(WRITE "${WORK}/outside/AllpowersMonitor.exe" "Original executable fixture\n")
file(SHA256 "${WORK}/outside/AllpowersMonitor.exe" original)
function(reject stage expected)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DALLPOWERS_ROOT=${ROOT}"
    "-DALLPOWERS_BUILD_ROOT=${WORK}/build" "-DALLPOWERS_DEPS_DIR=${WORK}/deps"
    "-DCPACK_PACKAGE_DIRECTORY=${WORK}/build"
    "-DCPACK_TEMPORARY_DIRECTORY=${stage}" -DCPACK_PACKAGE_VERSION=1.2.3
    -P "${ROOT}/cmake/PackWindows.cmake" RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "${expected}")
    message(FATAL_ERROR "Unexpected UPX staging result: ${result}\n${output}${error}")
  endif()
endfunction()
reject("${WORK}/outside" "only a CPack staging")
reject("${WORK}/build/_CPack_Packages/../../outside" "only a CPack staging")
reject("${WORK}/build/_CPack_Packages" "only a CPack staging")
file(MAKE_DIRECTORY "${WORK}/build/_CPack_Packages/missing")
file(WRITE "${WORK}/build/_CPack_Packages/missing/AllpowersMonitor.exe" "Incomplete stage\n")
reject("${WORK}/build/_CPack_Packages/missing" "Missing or symlinked staged")
file(SHA256 "${WORK}/outside/AllpowersMonitor.exe" after)
if(NOT after STREQUAL original OR EXISTS "${WORK}/deps")
  message(FATAL_ERROR "Rejected UPX stage changed original files or downloaded tools")
endif()
message(STATUS "UPX rejects original paths, traversal, package root and incomplete stages before mutation")
