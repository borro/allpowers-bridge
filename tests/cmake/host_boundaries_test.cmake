cmake_minimum_required(VERSION 4.4)
set(source "${WORK}/source")
file(MAKE_DIRECTORY "${source}/core/src" "${source}/apps/windows"
  "${source}/firmware/R2500Adapter")
file(WRITE "${source}/firmware/R2500Adapter/Device.h" "#pragma once\n")
file(WRITE "${source}/core/src/Shared.h" "#pragma once\n")
file(WRITE "${source}/apps/windows/client.cpp" "#include <Shared.h>\n")
function(check expected)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DROOT=${source}"
    -P "${ROOT}/cmake/scripts/quality/CheckHostBoundaries.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error)
  if(expected AND result)
    message(FATAL_ERROR "Valid host dependency rejected: ${out}${error}")
  elseif(NOT expected AND (NOT result OR NOT error MATCHES "must not include firmware"))
    message(FATAL_ERROR "Forbidden device dependency was not rejected: ${out}${error}")
  endif()
endfunction()
check(TRUE)
file(WRITE "${source}/core/src/Shared.h" "#include <Device.h>\n")
check(FALSE)
file(WRITE "${source}/core/src/Shared.h" "#pragma once\n")
file(WRITE "${source}/apps/windows/client.cpp"
  "#include \"../../firmware/R2500Adapter/../R2500Adapter/Device.h\"\n")
check(FALSE)
file(WRITE "${source}/apps/windows/client.cpp" "#include <Shared.h>\n")
check(TRUE)

file(WRITE "${source}/apps/windows/client.cpp"
  "#include \"${source}/firmware/R2500Adapter/Device.h\"\n")
check(FALSE)
file(WRITE "${source}/apps/windows/client.cpp" "#include <Shared.h>\n")
file(MAKE_DIRECTORY "${source}/drivers/windows")
file(WRITE "${source}/drivers/windows/driver.cpp" "#include <Device.h>\n")
check(FALSE)
file(WRITE "${source}/drivers/windows/driver.cpp" "#include <Shared.h>\n")
check(TRUE)

# Even when called directly, the product function must create no Windows device
# targets and must not discover the Arduino SDK or compiler.
file(WRITE "${source}/CMakeLists.txt" "cmake_minimum_required(VERSION 4.4)\nproject(HostBoundary NONE)\nset(CMAKE_HOST_SYSTEM_NAME Windows)\ninclude(\"${ROOT}/cmake/modules/products/Arduino.cmake\")\nap_add_firmware_targets(ROOT \"${source}\" BUILD_ROOT \"${WORK}/build\")\nget_property(targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)\nif(targets)\nmessage(FATAL_ERROR \"Windows firmware targets: \${targets}\")\nendif()\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${WORK}/build" -G Ninja
  "-DCMAKE_MAKE_PROGRAM=${NINJA}" COMMAND_ERROR_IS_FATAL ANY)

# Calling the build script directly must not bypass the platform boundary.
file(WRITE "${WORK}/windows-firmware.cmake"
  "set(CMAKE_HOST_SYSTEM_NAME Windows)\ninclude(\"${ROOT}/cmake/scripts/project/BuildFirmware.cmake\")\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${WORK}/windows-firmware.cmake"
  RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error)
if(NOT result OR NOT error MATCHES "Firmware compilation is Linux-only")
  message(FATAL_ERROR "Direct Windows firmware build was not rejected: ${out}${error}")
endif()
