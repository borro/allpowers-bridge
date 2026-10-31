find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(ALLPOWERS_FUZZ_SECONDS 120 CACHE STRING "Time budget per fuzzer in seconds")
if(NOT ALLPOWERS_FUZZ_SECONDS MATCHES "^[1-9][0-9]*$" OR ALLPOWERS_FUZZ_SECONDS GREATER 3600)
  message(FATAL_ERROR "ALLPOWERS_FUZZ_SECONDS must be an integer from 1 to 3600")
endif()
math(EXPR fuzz_timeout "${ALLPOWERS_FUZZ_SECONDS} + 60")
set(fuzz_output "${PROJECT_BINARY_DIR}/fuzz")
add_custom_target(fuzz-corpus
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/seed_corpus.py" "${fuzz_output}"
  VERBATIM)
add_custom_target(fuzz-tests)
foreach(subject ble hid_wire hid_server control)
  add_executable(fuzz_${subject} ${subject}.cpp)
  set_target_properties(fuzz_${subject} PROPERTIES CXX_SCAN_FOR_MODULES OFF INTERPROCEDURAL_OPTIMIZATION OFF)
  target_compile_features(fuzz_${subject} PRIVATE cxx_std_23)
  target_include_directories(fuzz_${subject} PRIVATE "${PROJECT_SOURCE_DIR}/core/src")
  target_compile_options(fuzz_${subject} PRIVATE -O1 -g -UNDEBUG
    -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(fuzz_${subject} PRIVATE -fsanitize=fuzzer,address,undefined)
  add_dependencies(fuzz-tests fuzz_${subject} fuzz-corpus)
  if(subject STREQUAL "control")
    add_test(NAME fuzz-policy COMMAND "${CMAKE_COMMAND}"
      "-DFUZZER=$<TARGET_FILE:fuzz_control>" "-DWORK=${fuzz_output}/policy"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/policy_test.cmake")
    set_tests_properties(fuzz-policy PROPERTIES LABELS fuzz TIMEOUT 30)
    continue()
  endif()
  if(subject STREQUAL "ble")
    set(max_length 1024)
  else()
    set(max_length 2048)
  endif()
  add_test(NAME fuzz-${subject} COMMAND fuzz_${subject}
    "${fuzz_output}/${subject}/corpus" -max_total_time=${ALLPOWERS_FUZZ_SECONDS} -max_len=${max_length}
    -timeout=5 -rss_limit_mb=1024 -seed=20261027 -print_final_stats=1
    "-artifact_prefix=${fuzz_output}/${subject}/crashes/")
  set_tests_properties(fuzz-${subject} PROPERTIES LABELS fuzz TIMEOUT ${fuzz_timeout})
endforeach()
