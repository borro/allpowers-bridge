cmake_minimum_required(VERSION 4.4)
if(NOT ROOT OR NOT WORK)
  message(FATAL_ERROR "ROOT and WORK are required")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../Dependencies.cmake")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${CMAKE_COMMAND}" -DKIND=workflow "-DWORK=${WORK}/tools"
  -P "${CMAKE_CURRENT_LIST_DIR}/../sdk/SetupAnalysisTools.cmake" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" -I -S -B "${CMAKE_CURRENT_LIST_DIR}/../../../tools/quality/static_analysis.py" workflow
  --root "${ROOT}" --output "${WORK}"
  --actionlint "${WORK}/tools/actionlint/actionlint"
  --shellcheck "${WORK}/tools/shellcheck-v${ALLPOWERS_SHELLCHECK_VERSION}/shellcheck"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" -I -S -B "${CMAKE_CURRENT_LIST_DIR}/../../../tools/quality/analysis_controls.py" workflow
  --root "${ROOT}" --actionlint "${WORK}/tools/actionlint/actionlint"
  --shellcheck "${WORK}/tools/shellcheck-v${ALLPOWERS_SHELLCHECK_VERSION}/shellcheck"
  COMMAND_ERROR_IS_FATAL ANY)
