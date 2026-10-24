cmake_minimum_required(VERSION 3.26)
include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
if(NOT CMAKE_HOST_WIN32)
  message(FATAL_ERROR "The MSVC toolchain check requires Windows")
endif()
string(REPLACE "." "\\." expected "${ALLPOWERS_MSVC_TOOLSET_VERSION}")
if(NOT "$ENV{VCToolsVersion}" MATCHES "^${expected}\\.")
  message(FATAL_ERROR
    "Expected MSVC toolset ${ALLPOWERS_MSVC_TOOLSET_VERSION}.x, got '$ENV{VCToolsVersion}'. "
    "Select Visual Studio 2026 and the pinned toolset before configuring Qt.")
endif()
message(STATUS "MSVC toolset verified: $ENV{VCToolsVersion}")
