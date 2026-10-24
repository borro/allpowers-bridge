cmake_minimum_required(VERSION 3.25)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${ROOT}/cmake/Version.cmake")
project(AllpowersUps LANGUAGES CXX)
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The UPS driver requires x64 MSVC and Windows SDK 10.0.26100.0")
endif()
include("${ROOT}/cmake/SetupWdk.cmake")
set(wdk "${ROOT}/build/deps/wdk-26100/c")
set(sdk "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
set(kit "10.0.26100.0")
if(NOT EXISTS "${sdk}/Include/${kit}/shared/batclass.h")
  message(FATAL_ERROR "Install Windows SDK ${kit} (shared headers required)")
endif()
set(CMAKE_CXX_STANDARD 17)
add_executable(AllpowersUps driver.cpp)
set_target_properties(AllpowersUps PROPERTIES SUFFIX .sys
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/package")
target_include_directories(AllpowersUps PRIVATE "${ROOT}/core/src"
  "${wdk}/Include/${kit}/km" "${wdk}/Include/wdf/kmdf/1.31"
  "${sdk}/Include/${kit}/shared" "${sdk}/Include/${kit}/ucrt")
target_compile_definitions(AllpowersUps PRIVATE _AMD64_ _WIN64 _KERNEL_MODE
  WINVER=0x0A00 _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A00000B
  KMDF_VERSION_MAJOR=1 KMDF_VERSION_MINOR=31)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ROOT}/core/src/UpsState.h" bridge.h)
file(SHA256 "${ROOT}/core/src/UpsState.h" state_hash)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/bridge.h" bridge_hash)
target_compile_definitions(AllpowersUps PRIVATE ALLPOWERS_STATE_HASH="${state_hash}${bridge_hash}")
target_compile_options(AllpowersUps PRIVATE /kernel /GR- /EHs-c- /Zl /W4 /utf-8)
target_link_options(AllpowersUps PRIVATE /DRIVER:WDM /SUBSYSTEM:NATIVE /ENTRY:FxDriverEntry
  /NODEFAULTLIB /MANIFEST:NO /INTEGRITYCHECK /DYNAMICBASE /NXCOMPAT /DEBUG /PDBALTPATH:AllpowersUps.pdb)
target_link_libraries(AllpowersUps PRIVATE
  "${wdk}/Lib/wdf/kmdf/x64/1.31/WdfDriverEntry.lib"
  "${wdk}/Lib/wdf/kmdf/x64/1.31/WdfLdr.lib"
  "${wdk}/Lib/${kit}/km/x64/ntoskrnl.lib"
  "${wdk}/Lib/${kit}/km/x64/battc.lib"
  "${wdk}/Lib/${kit}/km/x64/hal.lib"
  "${wdk}/Lib/${kit}/km/x64/BufferOverflowK.lib")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
configure_file(AllpowersUps.inf.in "package/AllpowersUps.inf" @ONLY)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/package/allpowersups.cat"
  COMMAND "${wdk}/tools/${kit}/x64/infverif.exe" /w "${CMAKE_BINARY_DIR}/package/AllpowersUps.inf"
  COMMAND "${wdk}/bin/${kit}/x86/Inf2Cat.exe" "/driver:${CMAKE_BINARY_DIR}/package" /os:10_X64 /uselocaltime
  COMMAND "${CMAKE_COMMAND}" "-DDRIVER=$<TARGET_FILE:AllpowersUps>" -P "${ROOT}/cmake/CheckKernelDriver.cmake"
  DEPENDS AllpowersUps "${CMAKE_BINARY_DIR}/package/AllpowersUps.inf" "${ROOT}/cmake/CheckKernelDriver.cmake"
  VERBATIM)
add_custom_target(ups-driver ALL DEPENDS "${CMAKE_BINARY_DIR}/package/allpowersups.cat")
