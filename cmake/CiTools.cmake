# Bootstrap with the runner's CMake; all actual builds use the pinned version.
cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
foreach(pair "cmake;ALLPOWERS_CMAKE_VERSION" "msvc;ALLPOWERS_MSVC_TOOLSET_VERSION" "sdk;ALLPOWERS_WINDOWS_SDK_VERSION"
    "fuzz-llvm;ALLPOWERS_FUZZ_LLVM_VERSION" "mull-llvm;ALLPOWERS_MULL_LLVM_VERSION" "gcc;ALLPOWERS_GCC_MAJOR"
    "tidy-llvm;ALLPOWERS_TIDY_LLVM_VERSION")
  list(GET pair 0 name)
  list(GET pair 1 variable)
  file(APPEND "$ENV{GITHUB_OUTPUT}" "${name}=${${variable}}\n")
endforeach()
