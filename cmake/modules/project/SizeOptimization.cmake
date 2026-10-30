include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

# Target-local policy: never change the portable core, firmware, driver or Debug.
function(ap_optimize_size)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "TARGET" "")
  ap_require_arguments(PREFIX AP REQUIRED TARGET)
  if(NOT MSVC)
    return()
  endif()
  # Keep ordinary relinks fast, including when reconfiguring an older IPO build.
  set_target_properties(${AP_TARGET} PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
    INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL OFF)
  target_compile_options(${AP_TARGET} PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release,MinSizeRel>>:/O1;/Gy;/Gw>")
  get_target_property(kind ${AP_TARGET} TYPE)
  if(NOT kind STREQUAL "STATIC_LIBRARY")
    target_link_options(${AP_TARGET} PRIVATE
      "$<$<CONFIG:Release,MinSizeRel>:/OPT:REF;/OPT:ICF;/INCREMENTAL:NO>")
  endif()
endfunction()
