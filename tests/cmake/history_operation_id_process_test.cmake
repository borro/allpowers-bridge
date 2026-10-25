cmake_minimum_required(VERSION 4.4)
if(NOT DEFINED PROBE OR NOT EXISTS "${PROBE}")
  message(FATAL_ERROR "History operation ID process probe is required")
endif()
execute_process(COMMAND "${PROBE}" --contract
  RESULT_VARIABLE contract_result ERROR_VARIABLE contract_error TIMEOUT 10)
if(NOT contract_result STREQUAL "0")
  message(FATAL_ERROR "History operation ID byte-order/zero contract failed: ${contract_result}; ${contract_error}")
endif()
foreach(attempt RANGE 1 2)
  # Each invocation starts a new executable. No application/Qt RNG state is
  # shared, so a per-process deterministic counter cannot satisfy this check.
  execute_process(COMMAND "${PROBE}" RESULT_VARIABLE process_result
    OUTPUT_VARIABLE nonce_${attempt} ERROR_VARIABLE process_error
    OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 10)
  if(NOT process_result STREQUAL "0")
    message(FATAL_ERROR "History operation ID process ${attempt} failed: ${process_result}; ${process_error}")
  endif()
  string(LENGTH "${nonce_${attempt}}" nonce_length)
  if(NOT nonce_length EQUAL 32 OR NOT nonce_${attempt} MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "History operation ID process ${attempt} did not return 128 bits as hexadecimal")
  endif()
endforeach()
if(nonce_1 STREQUAL nonce_2)
  message(FATAL_ERROR "Two independent process starts reused the same history operation nonce")
endif()
message(STATUS "Independent QtCore-only processes produced distinct 128-bit operation nonces; fallback byte-order and zero contracts passed")
