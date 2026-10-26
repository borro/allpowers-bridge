include_guard(GLOBAL)

# Target-local policy: never change the portable core, firmware, driver or Debug.
function(ap_optimize_size target)
  if(NOT MSVC)
    return()
  endif()
  # Keep ordinary relinks fast, including when reconfiguring an older IPO build.
  set_target_properties(${target} PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
    INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL OFF)
  target_compile_options(${target} PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release,MinSizeRel>>:/O1;/Gy;/Gw>")
  get_target_property(kind ${target} TYPE)
  if(NOT kind STREQUAL "STATIC_LIBRARY")
    target_link_options(${target} PRIVATE
      "$<$<CONFIG:Release,MinSizeRel>:/OPT:REF;/OPT:ICF;/INCREMENTAL:NO>")
  endif()
endfunction()
