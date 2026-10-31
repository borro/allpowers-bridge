cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/../../modules/quality/HostBoundaries.cmake")
ap_check_host_boundaries("${ROOT}")
message(STATUS "Core, Windows and host tests have no firmware header dependencies")
