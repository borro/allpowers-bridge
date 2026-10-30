include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../common/Files.cmake")

function(ap_configure_static_analysis)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(PROJECT_SOURCE_DIR "${AP_ROOT}")
  set(PROJECT_BINARY_DIR "${AP_BUILD_ROOT}")
  set(AP_ANALYSIS_ROOT "${AP_ROOT}")
  if(ALLPOWERS_STATIC_ANALYSIS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    if(MSVC)
      cmake_language(DEFER DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" CALL ap_write_analysis_index)
    endif()
  endif()
  return(PROPAGATE AP_ANALYSIS_ROOT CMAKE_EXPORT_COMPILE_COMMANDS)
endfunction()

function(ap_write_analysis_index)
  get_property(manifests GLOBAL PROPERTY AP_ANALYSIS_MANIFESTS)
  get_property(children GLOBAL PROPERTY AP_ANALYSIS_CHILDREN)
  list(JOIN manifests "\",\"" manifests)
  list(JOIN children "\",\"" children)
  if(manifests)
    set(manifests "\"${manifests}\"")
  endif()
  if(children)
    set(children "\"${children}\"")
  endif()
  ap_write_changed(PATH "${PROJECT_BINARY_DIR}/analysis-index.json"
    CONTENT "{\"manifests\":[${manifests}],\"children\":[${children}]}\n")
endfunction()

function(ap_enable_static_analysis)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "TARGET" "")
  ap_require_arguments(PREFIX AP REQUIRED TARGET)
  if(NOT ALLPOWERS_STATIC_ANALYSIS OR NOT MSVC)
    return()
  endif()
  set(rules "${AP_ANALYSIS_ROOT}/cmake/modules/quality/AnalysisRules.ruleset")
  get_filename_component(vs "$ENV{VCToolsInstallDir}/../../../.." ABSOLUTE)
  set(rule_directory "${vs}/Team Tools/Static Analysis Tools/Rule Sets")
  if(NOT EXISTS "${rule_directory}/NativeMinimumRules.ruleset")
    message(FATAL_ERROR "MSVC native analysis rules are missing: ${rule_directory}")
  endif()
  set(directory "${CMAKE_CURRENT_BINARY_DIR}/analysis/${AP_TARGET}")
  file(MAKE_DIRECTORY "${directory}")
  get_target_property(sources ${AP_TARGET} SOURCES)
  set(units "")
  foreach(source IN LISTS sources)
    if(source MATCHES "\\$<" OR NOT source MATCHES "\\.cpp$")
      continue()
    endif()
    get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    cmake_path(IS_PREFIX AP_ANALYSIS_ROOT "${source}" NORMALIZE inside)
    cmake_path(IS_PREFIX PROJECT_BINARY_DIR "${source}" NORMALIZE generated_directory)
    get_source_file_property(generated "${source}" GENERATED)
    if(NOT inside OR generated OR generated_directory OR NOT EXISTS "${source}")
      continue()
    endif()
    string(SHA256 key "${source}")
    set(report "${directory}/${key}.sarif")
    # Source flags exclude moc/rcc output. The target guard keeps these flags
    # out of other targets that happen to compile the same source file.
    set_property(SOURCE "${source}" APPEND PROPERTY COMPILE_OPTIONS
      "$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,${AP_TARGET}>:/analyze;/analyze:WX-;/analyze:external-;/external:W0;/external:env:INCLUDE;/analyze:ruleset${rules};/analyze:rulesetdirectory${rule_directory};/analyze:log${report};/analyze:log:format:sarif>")
    set_property(SOURCE "${source}" APPEND PROPERTY OBJECT_DEPENDS
      "${rules}" "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" "${rule_directory}/NativeMinimumRules.ruleset")
    if(units)
      string(APPEND units ",")
    endif()
    string(APPEND units "{\"source\":\"${source}\",\"report\":\"${report}\"}")
  endforeach()
  if(NOT units)
    message(FATAL_ERROR "No first-party sources selected for ${AP_TARGET}")
  endif()
  ap_write_changed(PATH "${directory}/analysis-manifest-${AP_TARGET}.json"
    CONTENT "{\"version\":\"${CMAKE_CXX_COMPILER_VERSION}\",\"rules\":\"${rules}\",\"units\":[${units}]}\n")
  set_property(GLOBAL APPEND PROPERTY AP_ANALYSIS_MANIFESTS "${directory}/analysis-manifest-${AP_TARGET}.json")
endfunction()

function(ap_add_analysis_targets)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(PROJECT_SOURCE_DIR "${AP_ROOT}")
  set(PROJECT_BINARY_DIR "${AP_BUILD_ROOT}")
  add_custom_target(workflow-lint
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${AP_ANALYSIS_ROOT}"
      "-DWORK=${PROJECT_BINARY_DIR}/workflow-lint" -P "${AP_ANALYSIS_ROOT}/cmake/scripts/quality/RunWorkflowLint.cmake"
    USES_TERMINAL VERBATIM)
  if(NOT ALLPOWERS_STATIC_ANALYSIS)
    return()
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  if(MSVC)
    set(report_command "${Python3_EXECUTABLE}" -I -S -B "${AP_ANALYSIS_ROOT}/tools/quality/static_analysis.py" msvc
      --root "${AP_ANALYSIS_ROOT}" --build "${PROJECT_BINARY_DIR}" --output "${PROJECT_BINARY_DIR}/analysis")
    # CI must be able to inspect a failed build without rebuilding it first.
    add_custom_target(static-analysis-report COMMAND ${report_command} USES_TERMINAL VERBATIM)
    add_custom_target(static-analysis COMMAND ${report_command}
      DEPENDS core USES_TERMINAL VERBATIM)
    if(ALLPOWERS_BUILD_WINDOWS)
      add_dependencies(static-analysis windows)
      set_property(GLOBAL APPEND PROPERTY AP_ANALYSIS_CHILDREN
        "${PROJECT_BINARY_DIR}/windows-monitor/analysis-index.json" "${PROJECT_BINARY_DIR}/ups-driver/analysis-index.json")
    endif()
  else()
    if(NOT BUILD_TESTING)
      message(FATAL_ERROR "Linux static analysis requires BUILD_TESTING for host firmware translation units")
    endif()
    find_program(ALLPOWERS_CLANG_TIDY NAMES "clang-tidy-${ALLPOWERS_TIDY_LLVM_VERSION}" REQUIRED)
    set(ALLPOWERS_CPPCHECK_ROOT "${PROJECT_BINARY_DIR}/tools/cppcheck" CACHE PATH "Pinned Cppcheck tool directory")
    add_custom_target(cppcheck-setup
      COMMAND "${CMAKE_COMMAND}" -DKIND=cppcheck "-DWORK=${ALLPOWERS_CPPCHECK_ROOT}"
        -P "${AP_ANALYSIS_ROOT}/cmake/scripts/sdk/SetupAnalysisTools.cmake"
      USES_TERMINAL VERBATIM)
    add_custom_target(static-analysis
      COMMAND "${Python3_EXECUTABLE}" -I -S -B "${AP_ANALYSIS_ROOT}/tools/quality/static_analysis.py" linux
        --root "${AP_ANALYSIS_ROOT}" --build "${PROJECT_BINARY_DIR}" --output "${PROJECT_BINARY_DIR}/analysis"
        --tidy "${ALLPOWERS_CLANG_TIDY}" --cppcheck "${ALLPOWERS_CPPCHECK_ROOT}/install/bin/cppcheck"
      COMMAND "${Python3_EXECUTABLE}" -I -S -B "${AP_ANALYSIS_ROOT}/tools/quality/analysis_controls.py" linux
        --root "${AP_ANALYSIS_ROOT}" --tidy "${ALLPOWERS_CLANG_TIDY}"
        --cppcheck "${ALLPOWERS_CPPCHECK_ROOT}/install/bin/cppcheck"
      DEPENDS core native-tests cppcheck-setup USES_TERMINAL VERBATIM)
  endif()
endfunction()
