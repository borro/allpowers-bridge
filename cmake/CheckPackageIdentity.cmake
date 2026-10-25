include("${CMAKE_CURRENT_LIST_DIR}/Version.cmake")
if(NOT ALLPOWERS_VERSION STREQUAL EXPECTED_VERSION)
  message(FATAL_ERROR "Build identity changed (${EXPECTED_VERSION} -> ${ALLPOWERS_VERSION}); reconfigure before packaging")
endif()
