# Maintained together with HistoryCompatibility.h and the offline maintenance
# validator. This describes archive capabilities, independently of product tags.
set(AP_HISTORY_CAPABILITY "ALLPOWERS_HISTORY_R12_W2_S32")
set(AP_HISTORY_STORAGE_JSON "{\"schema\":1,\"read_versions\":[1,2],\"write_version\":2,\"record_bytes\":32,\"capability\":\"${AP_HISTORY_CAPABILITY}\"}")

function(ap_history_metadata output binary)
  file(READ "${binary}" image HEX)
  string(HEX "${AP_HISTORY_CAPABILITY}" marker)
  string(FIND "${image}" "${marker}" position)
  if(position LESS 0)
    message(FATAL_ERROR "Compiled image lacks the AHP1/AHP2 storage capability")
  endif()
  set(${output} "${AP_HISTORY_STORAGE_JSON}" PARENT_SCOPE)
endfunction()

function(ap_history_validate manifest binary)
  ap_history_metadata(expected "${binary}")
  string(JSON actual ERROR_VARIABLE error GET "${manifest}" history_storage)
  string(JSON expected GET "${expected}")
  if(error OR NOT actual STREQUAL expected)
    message(FATAL_ERROR "Missing/incompatible archive metadata: unsafe downgrade package")
  endif()
endfunction()
