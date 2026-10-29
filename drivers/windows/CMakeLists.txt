cmake_minimum_required(VERSION 4.4)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${ROOT}/cmake/modules/project/Version.cmake")
ap_read_version(ROOT "${ROOT}")
include("${ROOT}/cmake/modules/project/Identity.cmake")
include("${ROOT}/cmake/Dependencies.cmake")
project(AllpowersUps LANGUAGES CXX)
ap_add_build_identity(ROOT "${ALLPOWERS_ROOT}" BUILD_ROOT "${PROJECT_BINARY_DIR}"
  RESOURCE_SOURCE "${PROJECT_SOURCE_DIR}")
include("${ALLPOWERS_ROOT}/cmake/modules/quality/Options.cmake")
ap_configure_quality(ROOT "${ALLPOWERS_ROOT}" BUILD_ROOT "${PROJECT_BINARY_DIR}")
include("${ROOT}/cmake/modules/project/WindowsCompiler.cmake")
ap_configure_windows_compiler()
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The UPS driver requires x64 MSVC and Windows SDK 10.0.26100.0")
endif()
set(ALLPOWERS_DEPS_DIR "${CMAKE_CURRENT_BINARY_DIR}/deps" CACHE PATH "Dependency cache")
set(wdk "${ALLPOWERS_DEPS_DIR}/wdk/${ALLPOWERS_WDK_VERSION}/c")
if(NOT EXISTS "${wdk}/Include/wdf/kmdf/${ALLPOWERS_KMDF_VERSION}/wdf.h")
  message(FATAL_ERROR "WDK is not prepared. Build the root ups-driver target or run cmake -DDEPS_DIR=<cache> -P cmake/scripts/sdk/SetupWdk.cmake first")
endif()
set(sdk "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
set(kit "${ALLPOWERS_WINDOWS_SDK_VERSION}")
if(NOT EXISTS "${sdk}/Include/${kit}/shared/batclass.h")
  message(FATAL_ERROR "Install Windows SDK ${kit} (shared headers required)")
endif()
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(AllpowersUps driver.cpp)
ap_enable_static_analysis(TARGET AllpowersUps)
add_dependencies(AllpowersUps product-identity)
target_compile_features(AllpowersUps PRIVATE cxx_std_23)
set_target_properties(AllpowersUps PROPERTIES SUFFIX .sys
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/package")
target_include_directories(AllpowersUps PRIVATE "${ROOT}/core/src")
target_include_directories(AllpowersUps SYSTEM PRIVATE
  "${wdk}/Include/${kit}/km" "${wdk}/Include/wdf/kmdf/${ALLPOWERS_KMDF_VERSION}"
  "${sdk}/Include/${kit}/shared" "${sdk}/Include/${kit}/ucrt")
string(REPLACE "." ";" kmdf_parts "${ALLPOWERS_KMDF_VERSION}")
list(GET kmdf_parts 0 kmdf_major)
list(GET kmdf_parts 1 kmdf_minor)
target_compile_definitions(AllpowersUps PRIVATE _AMD64_ _WIN64 _KERNEL_MODE
  WINVER=0x0A00 _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A00000B
  KMDF_VERSION_MAJOR=${kmdf_major} KMDF_VERSION_MINOR=${kmdf_minor})
target_compile_options(AllpowersUps PRIVATE /kernel /GR- /EHs-c- /Zl /W4 /utf-8)
target_link_options(AllpowersUps PRIVATE /DRIVER:WDM /SUBSYSTEM:NATIVE /ENTRY:FxDriverEntry
  /NODEFAULTLIB /MANIFEST:NO /INTEGRITYCHECK /DYNAMICBASE /NXCOMPAT /DEBUG /PDBALTPATH:AllpowersUps.pdb)
target_link_libraries(AllpowersUps PRIVATE
  "${wdk}/Lib/wdf/kmdf/x64/${ALLPOWERS_KMDF_VERSION}/WdfDriverEntry.lib"
  "${wdk}/Lib/wdf/kmdf/x64/${ALLPOWERS_KMDF_VERSION}/WdfLdr.lib"
  "${wdk}/Lib/${kit}/km/x64/ntoskrnl.lib"
  "${wdk}/Lib/${kit}/km/x64/battc.lib"
  "${wdk}/Lib/${kit}/km/x64/hal.lib"
  "${wdk}/Lib/${kit}/km/x64/BufferOverflowK.lib")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/package/allpowersups.cat"
  COMMAND "${wdk}/tools/${kit}/x64/infverif.exe" /w "${CMAKE_BINARY_DIR}/package/AllpowersUps.inf"
  COMMAND "${wdk}/bin/${kit}/x86/Inf2Cat.exe" "/driver:${CMAKE_BINARY_DIR}/package" /os:10_X64 /uselocaltime
  COMMAND "${CMAKE_COMMAND}" "-DDRIVER=$<TARGET_FILE:AllpowersUps>" -P "${ROOT}/cmake/scripts/quality/CheckKernelDriver.cmake"
  DEPENDS AllpowersUps "${CMAKE_BINARY_DIR}/package/AllpowersUps.inf" "${ROOT}/cmake/scripts/quality/CheckKernelDriver.cmake"
  VERBATIM)
add_custom_target(ups-driver ALL DEPENDS "${CMAKE_BINARY_DIR}/package/allpowersups.cat")
