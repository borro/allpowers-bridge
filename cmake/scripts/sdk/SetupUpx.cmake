cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/../../modules/sdk/Upx.cmake")
ap_setup_upx(DEPS_DIR "${DEPS_DIR}" WORK "${WORK}" OUTPUT UPX_EXECUTABLE)
