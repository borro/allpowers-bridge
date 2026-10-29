cmake_minimum_required(VERSION 4.4)
if(NOT ROOT)
  get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_CACHE=${ROOT}/build/deps/downloads"
  -P "${ROOT}/cmake/scripts/sdk/PrepareQtSources.cmake" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" "-DDEPS_DIR=${ROOT}/build/deps"
  -P "${ROOT}/cmake/scripts/sdk/SetupWdk.cmake" COMMAND_ERROR_IS_FATAL ANY)
set(result "${ROOT}/build/qt-sdk-root.txt")
file(REMOVE "${result}")
execute_process(COMMAND "${CMAKE_COMMAND}" -DMODE=Restore "-DROOT=${ROOT}" "-DRESULT_FILE=${result}"
  -P "${ROOT}/cmake/scripts/sdk/QtCache.cmake" COMMAND_ERROR_IS_FATAL ANY)
file(READ "${result}" qt_root)
if(NOT qt_root)
  message(STATUS "Building static Qt with 4 compiler jobs; Ninja prints compilation progress directly")
  execute_process(COMMAND "${CMAKE_COMMAND}" --preset windows -S "${ROOT}" -DALLPOWERS_QT_ROOT=
    -DALLPOWERS_QT_JOBS=4 -DALLPOWERS_BUILD_JOBS=4 -DALLPOWERS_QT_LOG_BUILD=OFF
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${ROOT}/build/windows" --target qt-static --parallel 2
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_COMMAND}" -DMODE=Seal "-DROOT=${ROOT}"
    -P "${ROOT}/cmake/scripts/sdk/QtCache.cmake" COMMAND_ERROR_IS_FATAL ANY)
endif()
