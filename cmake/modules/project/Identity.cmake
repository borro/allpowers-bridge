include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

function(ap_add_build_identity)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT;RESOURCE_SOURCE" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT RESOURCE_SOURCE)
  set(directory "${AP_BUILD_ROOT}/identity")
  set(ALLPOWERS_IDENTITY_FILE "${directory}/product-identity.cmake" PARENT_SCOPE)
  set(ALLPOWERS_IDENTITY_HEADER "${directory}/product-version.h" PARENT_SCOPE)
  # Supply initial files for generators/scanners. The same operation runs on
  # every build and rewrites only files whose contents actually changed.
  set(arguments "-DALLPOWERS_ROOT=${AP_ROOT}"
    "-DALLPOWERS_RELEASE_TAG=${ALLPOWERS_RELEASE_TAG}"
    "-DALLPOWERS_RELEASE_COMMIT=${ALLPOWERS_RELEASE_COMMIT}"
    "-DALLPOWERS_PARENT_IDENTITY=${ALLPOWERS_PARENT_IDENTITY}"
    "-DOUTPUT=${directory}" "-DRESOURCE_SOURCE=${AP_RESOURCE_SOURCE}")
  execute_process(COMMAND "${CMAKE_COMMAND}" ${arguments}
    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../scripts/project/BuildIdentity.cmake" COMMAND_ERROR_IS_FATAL ANY)
  set(resources)
  if(EXISTS "${AP_RESOURCE_SOURCE}/app.rc.in")
    list(APPEND resources "${directory}/AllpowersMonitor.rc" "${directory}/AllpowersWidget.rc")
  endif()
  if(EXISTS "${AP_RESOURCE_SOURCE}/AllpowersUps.inf.in")
    list(APPEND resources "${AP_BUILD_ROOT}/package/AllpowersUps.inf")
  endif()
  add_custom_target(product-identity ALL
    COMMAND "${CMAKE_COMMAND}" ${arguments} -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../scripts/project/BuildIdentity.cmake"
    BYPRODUCTS "${directory}/product-identity.cmake" "${directory}/product-version.h"
      "${directory}/product-version.txt" ${resources}
    VERBATIM)
endfunction()
