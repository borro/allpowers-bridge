cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/../../modules/sdk/Ninja.cmake")
ap_setup_ninja(DESTINATION "${DESTINATION}" OUTPUT NINJA_EXECUTABLE)
# CI environment integration belongs to the executable entry, not the installer.
if(NOT "$ENV{GITHUB_PATH}" STREQUAL "")
  cmake_path(GET NINJA_EXECUTABLE PARENT_PATH destination)
  file(APPEND "$ENV{GITHUB_PATH}" "${destination}\n")
endif()
