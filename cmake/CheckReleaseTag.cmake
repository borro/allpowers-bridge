cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/ReleaseVersion.cmake")
ap_check_release_tag("${ROOT}" "${TAG}" version "${COMMIT}")
message(STATUS "Verified release tag: ${TAG} -> ${version}")
