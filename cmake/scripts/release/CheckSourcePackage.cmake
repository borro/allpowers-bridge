cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/../../modules/common/Files.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../modules/project/BuildParallelism.cmake")
ap_configure_parallelism()
if(NOT BUILD_ROOT)
  message(FATAL_ERROR "Set BUILD_ROOT to the configured binary directory")
endif()
file(REAL_PATH "${BUILD_ROOT}" build_root)
file(MAKE_DIRECTORY "${WORK}")
file(REAL_PATH "${WORK}" work_path)
ap_remove_tree(ROOT "${build_root}" PATH "${work_path}")
file(MAKE_DIRECTORY "${work_path}")
file(READ "${ARCHIVE}.sha256" checksum)
string(SUBSTRING "${checksum}" 0 64 expected)
file(SHA256 "${ARCHIVE}" actual)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "Source package checksum mismatch")
endif()
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${work_path}")
if(EXISTS "${work_path}/allpowers/.git" OR EXISTS "${work_path}/allpowers/.local")
  message(FATAL_ERROR "Private Git/configuration leaked into the source kit")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../modules/common/PublicSources.cmake")
ap_public_sources(OUTPUT expected_files ROOT "${work_path}/allpowers")
list(APPEND expected_files VERSION SOURCE_FILES)
list(SORT expected_files)
file(GLOB_RECURSE actual_files LIST_DIRECTORIES FALSE RELATIVE "${work_path}/allpowers"
  "${work_path}/allpowers/*" "${work_path}/allpowers/.*")
list(SORT actual_files)
if(NOT actual_files STREQUAL expected_files)
  message(FATAL_ERROR "Source archive contents differ from the public inventory")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${work_path}/allpowers" -B "${work_path}/build"
  -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
  -DBUILD_TESTING=ON COMMAND_ERROR_IS_FATAL ANY)
file(READ "${work_path}/build/identity/product-version.txt" rebuilt_version)
string(STRIP "${rebuilt_version}" rebuilt_version)
if(NOT rebuilt_version STREQUAL VERSION)
  message(FATAL_ERROR "Source archive lost its Git-tag-derived version")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${work_path}/build" --target core native-tests
  --parallel "${ALLPOWERS_BUILD_JOBS}"
  OUTPUT_FILE "${work_path}/build.log" ERROR_FILE "${work_path}/build.log" RESULT_VARIABLE result)
if(result)
  message(FATAL_ERROR "Source-kit native build failed; see ${work_path}/build.log")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${work_path}/build" --output-on-failure --parallel 4
  COMMAND_ERROR_IS_FATAL ANY)
if(CMAKE_HOST_WIN32)
  if(NOT QT_ROOT)
    message(FATAL_ERROR "Windows source-kit verification requires the matching static Qt SDK")
  endif()
  # Prove the desktop product and its tests need only core, not device headers.
  # This is the disposable, verified extraction under this build directory.
  ap_remove_tree(ROOT "${work_path}" PATH "${work_path}/allpowers/firmware")
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${work_path}/allpowers/apps/windows"
    -B "${work_path}/windows" -G Ninja -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DBUILD_TESTING=ON
    "-DCMAKE_PREFIX_PATH=${QT_ROOT}" "-DALLPOWERS_QT_SOURCE_CACHE=${work_path}/qt-source"
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${work_path}/windows" --parallel "${ALLPOWERS_BUILD_JOBS}"
    OUTPUT_FILE "${work_path}/windows-build.log" ERROR_FILE "${work_path}/windows-build.log"
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${work_path}/windows"
    -R "^(desktop|widget|languages|archive-transport|history-operation-process|single-exe|widget-single-exe|integration-contract|usb-serial-contract|hid-session|hid-native|runtime-log|battery-contract|service-contract|cli-contract(-.*)?|ipc-contract)$"
    --parallel 4 --output-on-failure COMMAND_ERROR_IS_FATAL ANY)
endif()
message(STATUS "Source package rebuilt and passed core and supported desktop checks without .git or Python; desktop built without firmware/")
