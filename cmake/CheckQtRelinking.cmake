cmake_minimum_required(VERSION 4.4)
# An isolated consumer rebuild. Never modifies the production Qt SDK or sources.
include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
if(NOT WIN32 AND NOT CMAKE_HOST_WIN32)
  message(FATAL_ERROR "The relinking check requires an MSVC developer environment")
endif()
foreach(required ROOT WORK SOURCE_CACHE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "Set ${required}")
  endif()
endforeach()
if(NOT JOBS)
  set(JOBS 4)
endif()
file(MAKE_DIRECTORY "${WORK}")
file(REAL_PATH "${WORK}" work)
file(REAL_PATH "${ROOT}" root)
if(NOT BUILD_ROOT)
  set(BUILD_ROOT "${root}")
endif()
file(REAL_PATH "${BUILD_ROOT}" build_root)
cmake_path(IS_PREFIX build_root "${work}" NORMALIZE inside)
if(NOT inside OR work STREQUAL root OR work STREQUAL build_root)
  message(FATAL_ERROR "Relinking check must use a separate subdirectory of BUILD_ROOT")
endif()
set(prefix "${work}/qt-install")
file(MAKE_DIRECTORY "${work}/tmp")
set(ENV{TMP} "${work}/tmp")
set(ENV{TEMP} "${work}/tmp")
set(marker "allpowers-relink-proof")
foreach(module qtbase qtconnectivity)
  string(TOUPPER "${module}" key)
  set(archive "${SOURCE_CACHE}/${module}-everywhere-src-${ALLPOWERS_QT_VERSION}.tar.xz")
  file(SHA256 "${archive}" digest)
  if(NOT digest STREQUAL ALLPOWERS_${key}_SHA256)
    message(FATAL_ERROR "Wrong corresponding source archive: ${module}")
  endif()
  set(source "${work}/sources/${module}-everywhere-src-${ALLPOWERS_QT_VERSION}")
  if(NOT EXISTS "${source}/CMakeLists.txt")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${work}/sources")
  endif()
  if(module STREQUAL qtbase)
    set(changed "${source}/src/corelib/global/qlibraryinfo.cpp")
    file(READ "${changed}" code)
    if(NOT code MATCHES "${marker}")
      set(original "return QT_VERSION_STR;")
      string(FIND "${code}" "${original}" found)
      if(found LESS 0)
        message(FATAL_ERROR "Cannot locate the qVersion implementation")
      endif()
      string(REPLACE "${original}"
        "return QT_VERSION_STR \"-${marker}\"; // ALLPOWERS consumer relinking test, 2026-10-25"
        code "${code}")
      file(WRITE "${changed}" "${code}")
    endif()
    set(features -DFEATURE_cpp_winrt=ON -DFEATURE_static_runtime=ON -DFEATURE_opengl=OFF
      -DINPUT_opengl=no -DFEATURE_opengl_dynamic=OFF -DFEATURE_opengl_desktop=OFF -DFEATURE_icu=OFF
      -DFEATURE_system_zlib=OFF -DFEATURE_system_png=OFF -DFEATURE_system_jpeg=OFF
      -DFEATURE_system_freetype=OFF -DFEATURE_system_harfbuzz=OFF -DFEATURE_system_sqlite=OFF)
  else()
    set(features -DFEATURE_winrt_bt=ON)
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${work}/${module}-build" -G Ninja
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
    "-DCMAKE_CXX_STANDARD=${ALLPOWERS_CXX_STANDARD}" -DCMAKE_CXX_STANDARD_REQUIRED=ON
    -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    "-DCMAKE_PROJECT_INCLUDE=${root}/cmake/WindowsCompiler.cmake"
    "-DCMAKE_INSTALL_PREFIX=${prefix}" "-DCMAKE_PREFIX_PATH=${prefix}" ${features}
    OUTPUT_FILE "${work}/${module}-configure.log" ERROR_FILE "${work}/${module}-configure.log"
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${work}/${module}-build" --parallel "${JOBS}"
    OUTPUT_FILE "${work}/${module}-build.log" ERROR_FILE "${work}/${module}-build.log"
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_COMMAND}" --install "${work}/${module}-build"
    OUTPUT_FILE "${work}/${module}-install.log" ERROR_FILE "${work}/${module}-install.log"
    COMMAND_ERROR_IS_FATAL ANY)
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${root}/apps/windows" -B "${work}/applications" -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
  -DBUILD_TESTING=ON "-DCMAKE_PREFIX_PATH=${prefix}"
  "-DALLPOWERS_QT_SOURCE_CACHE=${SOURCE_CACHE}"
  OUTPUT_FILE "${work}/applications-configure.log" ERROR_FILE "${work}/applications-configure.log"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${work}/applications" --parallel "${JOBS}"
  OUTPUT_FILE "${work}/applications-build.log" ERROR_FILE "${work}/applications-build.log"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${work}/applications"
  -R "^(desktop|widget|single-exe|widget-single-exe)$" --timeout 180 --output-on-failure
  OUTPUT_FILE "${work}/applications-tests.log" ERROR_FILE "${work}/applications-tests.log"
  COMMAND_ERROR_IS_FATAL ANY)
foreach(app AllpowersMonitor AllpowersWidget)
  execute_process(COMMAND "${work}/applications/${app}.exe" --self-test
    --report "${work}/${app}.json" -platform offscreen TIMEOUT 30 COMMAND_ERROR_IS_FATAL ANY)
  file(READ "${work}/${app}.json" report)
  string(JSON runtime GET "${report}" qt_runtime)
  string(JSON status GET "${report}" status)
  if(NOT runtime STREQUAL "${ALLPOWERS_QT_VERSION}-${marker}" OR NOT status STREQUAL "PASS")
    message(FATAL_ERROR "${app} did not execute the modified Qt library")
  endif()
endforeach()
file(WRITE "${work}/RESULT.txt"
  "PASS: both applications rebuilt and ran offline with Qt ${ALLPOWERS_QT_VERSION}-${marker}.\nProduction SDK and devices were not modified.\n")
message(STATUS "Modified Qt relinking and execution passed for both applications")
