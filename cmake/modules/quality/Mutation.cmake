include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

function(ap_configure_mutation)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(PROJECT_SOURCE_DIR "${AP_ROOT}")
  set(PROJECT_BINARY_DIR "${AP_BUILD_ROOT}")
  if(NOT ALLPOWERS_MUTATION)
    return()
  endif()
  set(ALLPOWERS_MULL_ROOT "${PROJECT_BINARY_DIR}/tools/mull/install/usr" CACHE PATH "Extracted Mull prefix")
  find_program(ALLPOWERS_MULL_RUNNER NAMES mull-runner-${ALLPOWERS_MULL_LLVM_VERSION}
    PATHS "${ALLPOWERS_MULL_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
  find_program(ALLPOWERS_MULL_REPORTER NAMES mull-reporter-${ALLPOWERS_MULL_LLVM_VERSION}
    PATHS "${ALLPOWERS_MULL_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
  find_file(ALLPOWERS_MULL_PLUGIN NAMES mull-ir-frontend-${ALLPOWERS_MULL_LLVM_VERSION}
    PATHS "${ALLPOWERS_MULL_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(ALLPOWERS_MULL_CONFIG "${PROJECT_BINARY_DIR}/mull.yml")
  # Paths are regexes, so quote metacharacters in arbitrary checkout directories.
  string(REGEX REPLACE "([][+.*()^$?|\\\\])" "\\\\\\1" mutation_root "${PROJECT_SOURCE_DIR}")
  string(CONCAT config
    "mutators: [cxx_comparison, cxx_boundary, cxx_arithmetic, cxx_bitwise, cxx_remove_negation]\n"
    "includePaths:\n  - '^${mutation_root}/core/'\n  - '^${mutation_root}/firmware/R2500Adapter/'\n  - '^${mutation_root}/apps/windows/ups_state[.]h$'\n"
    "compilationDatabasePath: '${PROJECT_BINARY_DIR}/compile_commands.json'\n"
    "parallelization:\n  workers: 2\n  executionWorkers: 2\n")
  file(CONFIGURE OUTPUT "${ALLPOWERS_MULL_CONFIG}" CONTENT "${config}" @ONLY)
  return(PROPAGATE CMAKE_EXPORT_COMPILE_COMMANDS ALLPOWERS_MULL_CONFIG)
endfunction()

function(ap_enable_mutation)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "TARGET" "")
  ap_require_arguments(PREFIX AP REQUIRED TARGET)
  if(NOT ALLPOWERS_MUTATION)
    return()
  endif()
  set_target_properties(${AP_TARGET} PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF
    CXX_SCAN_FOR_MODULES OFF
    INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
    CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;MULL_CONFIG=${ALLPOWERS_MULL_CONFIG}")
  target_compile_options(${AP_TARGET} PRIVATE -O0 -g -grecord-command-line
    "-fpass-plugin=${ALLPOWERS_MULL_PLUGIN}")
  get_target_property(sources ${AP_TARGET} SOURCES)
  foreach(source IN LISTS sources)
    set_property(SOURCE "${source}" APPEND PROPERTY OBJECT_DEPENDS "${ALLPOWERS_MULL_CONFIG}")
  endforeach()
endfunction()

function(ap_add_mutation_target)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(PROJECT_SOURCE_DIR "${AP_ROOT}")
  set(PROJECT_BINARY_DIR "${AP_BUILD_ROOT}")
  if(NOT ALLPOWERS_MUTATION)
    return()
  endif()
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
  set(control_config "${PROJECT_BINARY_DIR}/mull-control.yml")
  string(CONCAT config
    "mutators: [cxx_ge_to_gt, cxx_add_to_sub, cxx_sub_to_add]\n"
    "includePaths: ['mutation_subject[.]h$']\n"
    "compilationDatabasePath: '${PROJECT_BINARY_DIR}/compile_commands.json'\n"
    "parallelization: {workers: 2, executionWorkers: 2}\n")
  file(CONFIGURE OUTPUT "${control_config}" CONTENT "${config}" @ONLY)
  add_executable(mutation_control EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/tests/native/mutation_control.cpp")
  target_compile_features(mutation_control PRIVATE cxx_std_23)
  ap_enable_mutation(TARGET mutation_control)
  target_compile_options(mutation_control PRIVATE -UNDEBUG)
  set_target_properties(mutation_control PROPERTIES CXX_COMPILER_LAUNCHER
    "${CMAKE_COMMAND};-E;env;MULL_CONFIG=${control_config}")
  set_property(SOURCE "${PROJECT_SOURCE_DIR}/tests/native/mutation_control.cpp"
    APPEND PROPERTY OBJECT_DEPENDS "${control_config}")
  add_custom_target(mutation-policy-check
    COMMAND "${CMAKE_COMMAND}" -E env "MULL_CONFIG=${control_config}"
      "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/maintenance/mutation_integration.py"
      "${ALLPOWERS_MULL_RUNNER}" "${ALLPOWERS_MULL_REPORTER}" "$<TARGET_FILE:mutation_control>"
      "${PROJECT_BINARY_DIR}/mutation-policy"
    DEPENDS mutation_control USES_TERMINAL VERBATIM)
  # Explicit source/test pairs also scan the shared library, not only executable TUs.
  set(pairs "")
  foreach(target models_test abi_test ups_test firmware_test history_rollback_test)
    string(APPEND pairs "[\"$<TARGET_FILE:${target}>\",\"$<TARGET_FILE:${target}>\"],\n")
  endforeach()
  foreach(target abi_test ups_test)
    string(APPEND pairs "[\"$<TARGET_FILE:allpowers_core>\",\"$<TARGET_FILE:${target}>\"],\n")
  endforeach()
  string(REGEX REPLACE ",\n$" "" pairs "${pairs}")
  file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/mutation-targets.json" CONTENT "[${pairs}]\n")
  set(library_args --library-dir "$<TARGET_FILE_DIR:allpowers_core>")
  foreach(directory IN LISTS CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES)
    list(APPEND library_args --library-dir "${directory}")
  endforeach()
  add_custom_target(mutation-report
    COMMAND "${CMAKE_COMMAND}" -E env "MULL_CONFIG=${ALLPOWERS_MULL_CONFIG}"
      "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/quality/mutation_report.py"
      --runner "${ALLPOWERS_MULL_RUNNER}" --reporter "${ALLPOWERS_MULL_REPORTER}"
      --manifest "${PROJECT_BINARY_DIR}/mutation-targets.json"
      --baseline "${PROJECT_SOURCE_DIR}/tests/mutation/accepted.json"
      --root "${PROJECT_SOURCE_DIR}" --output "${PROJECT_BINARY_DIR}/mutation"
      ${library_args}
    DEPENDS native-tests mutation-policy-check USES_TERMINAL VERBATIM)
endfunction()
