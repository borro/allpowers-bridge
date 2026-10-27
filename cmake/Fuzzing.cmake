include_guard(GLOBAL)
option(ALLPOWERS_FUZZING "Fuzz portable BLE and CDC parsers with libFuzzer and ASan/UBSan" OFF)
if(ALLPOWERS_FUZZING)
  if(NOT BUILD_TESTING OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
      NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
      NOT CMAKE_CXX_COMPILER_VERSION MATCHES "^${ALLPOWERS_FUZZ_LLVM_VERSION}[.]")
    message(FATAL_ERROR "Fuzzing requires BUILD_TESTING and Clang ${ALLPOWERS_FUZZ_LLVM_VERSION} on Linux")
  endif()
  if(ALLPOWERS_COVERAGE OR ALLPOWERS_SANITIZERS OR ALLPOWERS_MUTATION)
    message(FATAL_ERROR "Use a separate fuzz build; fuzz targets enable their own ASan/UBSan")
  endif()
endif()
