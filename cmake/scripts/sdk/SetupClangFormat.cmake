cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/../../modules/sdk/ClangFormat.cmake")
ap_setup_clang_format(WORK "${WORK}" OUTPUT CLANG_FORMAT_TOOL)
