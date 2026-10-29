include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

function(ap_enable_sanitizers)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "TARGET" "")
  ap_require_arguments(PREFIX AP REQUIRED TARGET)
  if(NOT ALLPOWERS_SANITIZERS)
    return()
  endif()
  target_compile_options(${AP_TARGET} PRIVATE -fsanitize=address,undefined
    -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  target_link_options(${AP_TARGET} PRIVATE -fsanitize=address,undefined)
endfunction()
