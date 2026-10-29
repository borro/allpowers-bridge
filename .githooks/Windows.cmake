cmake_minimum_required(VERSION 4.4)
# The only platform adapter needed by native Windows hook tasks.
include("${CMAKE_CURRENT_LIST_DIR}/Host.cmake")
ap_hook_host_tools(CONFIG "${CONFIG}" OUTPUT ninja)
foreach(key vcvars windowsCMake windowsNinja windowsDeps windowsPython qt windowsCoverage)
  ap_hook_setting(NAME "${key}" OUTPUT "${key}")
endforeach()
set(temporary "${ROOT}/build/git-hooks/temp/${TASK}")
file(MAKE_DIRECTORY "${temporary}")
set(ENV{TEMP} "${temporary}")
set(ENV{TMP} "${temporary}")
# Pass the batch path through the environment, without interpolating shell code.
set(ENV{ALLPOWERS_HOOK_VCVARS} "${vcvars}")
execute_process(COMMAND "${CMAKE_CURRENT_LIST_DIR}/WindowsEnvironment.cmd"
  OUTPUT_VARIABLE environment COMMAND_ERROR_IS_FATAL ANY)
unset(ENV{ALLPOWERS_HOOK_VCVARS})
string(REPLACE ";" "\\;" environment "${environment}")
string(REPLACE "\r\n" "\n" environment "${environment}")
string(REPLACE "\n" ";" environment "${environment}")
foreach(line IN LISTS environment)
  if(line MATCHES "^([^=]+)=(.*)$")
    set("ENV{${CMAKE_MATCH_1}}" "${CMAKE_MATCH_2}")
  endif()
endforeach()
cmake_path(GET windowsCMake PARENT_PATH cmake_bin)
cmake_path(GET windowsNinja PARENT_PATH ninja_bin)
set(ENV{PATH} "${cmake_bin};${ninja_bin};$ENV{PATH}")
execute_process(COMMAND "${windowsCMake}" "-DROOT=${SOURCE}" "-DTASK=${TASK}"
  "-DNINJA=${windowsNinja}" "-DDEPS=${windowsDeps}" "-DPYTHON=${windowsPython}"
  "-DQT=${qt}" "-DCOVERAGE_TOOL=${windowsCoverage}" -P "${ROOT}/cmake/scripts/hooks/RunHookCheck.cmake"
  WORKING_DIRECTORY "${ROOT}" COMMAND_ERROR_IS_FATAL ANY)
