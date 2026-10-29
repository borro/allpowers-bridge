cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/../../modules/release/ReleaseVersion.cmake")
ap_check_release_tag(ROOT "${ROOT}" TAG "${TAG}" OUTPUT version COMMIT "${COMMIT}")
message(STATUS "Verified release tag: ${TAG} -> ${version}")
