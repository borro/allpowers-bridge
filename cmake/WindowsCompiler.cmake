include_guard(GLOBAL)

# CMake still maps C++23 to /std:c++latest. MSVC's dedicated C++23 mode
# avoids opting into the next draft standard while preserving CMake features.
if(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.43)
  set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std:c++23preview")
  set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "-std:c++23preview")
endif()

# CMake 3.31's MSVC probe decodes /showIncludes with ENCODING AUTO. On some
# localized Windows installations this corrupts the prefix while Ninja sees
# the original bytes. Probe the same compiler with ENCODING NONE so Ninja can
# record real header dependencies; do not hash headers or force recompilation.
function(ap_configure_msvc_dependencies)
  if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "^Ninja")
    return()
  endif()
  set(probe "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/AllpowersShowIncludes")
  file(MAKE_DIRECTORY "${probe}")
  file(WRITE "${probe}/dependency.h" "\n")
  file(WRITE "${probe}/probe.cpp" "#include \"dependency.h\"\nint probe() { return 0; }\n")
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c probe.cpp
    WORKING_DIRECTORY "${probe}" OUTPUT_VARIABLE output ERROR_VARIABLE error
    RESULT_VARIABLE result ENCODING NONE)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot verify MSVC header dependency tracking: ${error}")
  endif()
  # This is CMake's own prefix shape: localized text, followed by an absolute path.
  if(NOT output MATCHES "(^|\n)([^:\n][^:\n]+:[^:\n]*[^: \n][^: \n]:?[ \t]+)([A-Za-z]:)")
    message(FATAL_ERROR "MSVC /showIncludes returned no recognizable dependency prefix")
  endif()
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()
ap_configure_msvc_dependencies()
