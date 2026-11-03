cmake_minimum_required(VERSION 4.4)
file(MAKE_DIRECTORY "${WORK}/source" "${WORK}/run")
file(WRITE "${WORK}/source/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 4.4)
project(StandaloneSmokeControl LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
add_executable(smoke_control main.cpp)
target_compile_features(smoke_control PRIVATE cxx_std_23)
]=])
file(WRITE "${WORK}/source/main.cpp" [=[
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
int main(int argc, char **argv) {
  std::string mode = "system", report;
  bool selfTest = false, offscreen = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--self-test") selfTest = true;
    else if (arg == "--language" && i + 1 < argc) mode = argv[++i];
    else if (arg == "--report" && i + 1 < argc) report = argv[++i];
    else if (arg == "-platform" && i + 1 < argc) offscreen = std::string(argv[++i]) == "offscreen";
  }
  if (!selfTest || !offscreen || report.empty() || std::getenv("QT_PLUGIN_PATH") ||
      std::getenv("QT_QPA_PLATFORM_PLUGIN_PATH") || std::getenv("QT_QPA_PLATFORM")) return 9;
  std::puts("control process started"); std::fflush(stdout);
  std::fputs("control diagnostics\n", stderr); std::fflush(stderr);
  if (mode == "slow") std::this_thread::sleep_for(std::chrono::seconds(21));
  if (mode == "hang") std::this_thread::sleep_for(std::chrono::seconds(90));
  if (mode == "exit") return 7;
  std::ofstream out(report);
  out << (mode == "failure" ? "{\"status\":\"FAIL\",\"error\":\"control failure\"}"
                            : "{\"status\":\"PASS\"}");
  return mode == "failure" ? 3 : 0;
}
]=])
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${WORK}/source" -B "${WORK}/build" -G Ninja
  "-DCMAKE_CXX_COMPILER=${CXX}" "-DCMAKE_MAKE_PROGRAM=${NINJA}" -DCMAKE_BUILD_TYPE=Release
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}/build" --parallel 2
  COMMAND_ERROR_IS_FATAL ANY)
file(COPY_FILE "${WORK}/build/smoke_control.exe" "${WORK}/run/smoke_control.exe")
file(CONFIGURE OUTPUT "${WORK}/probe.cmake" CONTENT [=[
include("@ROOT@/cmake/modules/quality/StandaloneSmoke.cmake")
set(options)
if(DEFINED LIMIT)
  list(APPEND options TIMEOUT "${LIMIT}")
endif()
ap_run_smoke(WORK "@WORK@/run" EXE_NAME smoke_control.exe LANGUAGE "${MODE}"
  REPORT_PATH "@WORK@/run/report.json" ${options})
]=] @ONLY)
set(ENV{QT_PLUGIN_PATH} "must-be-unset")
set(ENV{QT_QPA_PLATFORM_PLUGIN_PATH} "must-be-unset")
set(ENV{QT_QPA_PLATFORM} "must-be-unset")
foreach(mode slow exit failure hang)
  # Failed or hung children must not inherit a previous PASS report.
  file(WRITE "${WORK}/run/report.json" "{\"status\":\"PASS\",\"stale\":true}")
  set(options)
  if(mode STREQUAL hang)
    set(options -DLIMIT=1)
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DMODE=${mode}" ${options} -P "${WORK}/probe.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 75)
  file(WRITE "${WORK}/${mode}.log" "${output}${errors}")
  if(mode STREQUAL slow)
    if(NOT result STREQUAL "0")
      message(FATAL_ERROR "Healthy startup beyond the former 20s limit failed: ${output}${errors}")
    endif()
    file(READ "${WORK}/run/report.json" report)
    if(NOT report STREQUAL "{\"status\":\"PASS\"}")
      message(FATAL_ERROR "Slow startup did not produce a fresh report")
    endif()
  else()
    if(result STREQUAL "0" OR NOT "${output}${errors}" MATCHES "standalone self-test failed")
      message(FATAL_ERROR "Failed/hung control was not rejected: ${mode}: ${output}${errors}")
    endif()
    if(mode STREQUAL failure)
      if(NOT "${output}${errors}" MATCHES "control failure")
        message(FATAL_ERROR "Failure report was lost")
      endif()
    elseif(EXISTS "${WORK}/run/report.json" OR NOT "${output}${errors}" MATCHES "No JSON report")
      message(FATAL_ERROR "Stale report survived a failed/hung child")
    endif()
    if(mode STREQUAL hang AND NOT "${output}${errors}" MATCHES "timeout")
      message(FATAL_ERROR "Hang was not stopped by the process deadline")
    endif()
  endif()
  file(READ "${WORK}/run/self-test-${mode}-out.log" stdout)
  file(READ "${WORK}/run/self-test-${mode}-err.log" stderr)
  if(NOT stdout MATCHES "elapsed=.*control process started" OR NOT stderr MATCHES "control diagnostics")
    message(FATAL_ERROR "Child output or timing diagnostics were lost: ${mode}")
  endif()
endforeach()
message(STATUS "Slow startup succeeds; nonzero exit, FAIL report and hang remain fatal with fresh diagnostics")
