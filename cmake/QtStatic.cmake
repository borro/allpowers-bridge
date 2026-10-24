include(ExternalProject)
set(ALLPOWERS_QT_JOBS 2 CACHE STRING "Parallel Qt compiler jobs (bounded for hosted CI runners)")
set(ALLPOWERS_QT_ROOT "" CACHE PATH "Optional prebuilt static Qt MSVC installation")
set(ALLPOWERS_QT_SOURCE_CACHE "${ALLPOWERS_DEPS_DIR}/downloads" CACHE PATH "Verified Qt source archives")
if(ALLPOWERS_QT_ROOT)
  if(NOT EXISTS "${ALLPOWERS_QT_ROOT}/lib/cmake/Qt6Bluetooth/Qt6BluetoothConfig.cmake")
    message(FATAL_ERROR "ALLPOWERS_QT_ROOT must point to a complete static Qt installation")
  endif()
  add_custom_target(qt-static)
  return()
endif()
# Managed installs belong to this build tree and keep their producer targets
# even after installation. An existing config file must not disable the graph.
set(ALLPOWERS_QT_ROOT "${PROJECT_BINARY_DIR}/deps/qt/install")
set(qt_base_url "${ALLPOWERS_QT_SOURCE_URL}")
set(qt_common
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
  "-DCMAKE_CXX_STANDARD=${ALLPOWERS_CXX_STANDARD}" -DCMAKE_CXX_STANDARD_REQUIRED=ON
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
  "-DCMAKE_PROJECT_INCLUDE=${CMAKE_CURRENT_LIST_DIR}/WindowsCompiler.cmake"
  "-DCMAKE_INSTALL_PREFIX=${ALLPOWERS_QT_ROOT}")
ExternalProject_Add(qtbase-static
  URL "${qt_base_url}/qtbase-everywhere-src-${ALLPOWERS_QT_VERSION}.tar.xz"
  URL_HASH "SHA256=${ALLPOWERS_QTBASE_SHA256}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  TIMEOUT 180 INACTIVITY_TIMEOUT 30
  DOWNLOAD_DIR "${ALLPOWERS_QT_SOURCE_CACHE}"
  PREFIX "${PROJECT_BINARY_DIR}/deps/qtbase"
  CMAKE_GENERATOR Ninja
  CMAKE_ARGS ${qt_common} -DFEATURE_cpp_winrt=ON -DFEATURE_static_runtime=ON -DFEATURE_opengl=OFF
    -DINPUT_opengl=no -DFEATURE_opengl_dynamic=OFF -DFEATURE_opengl_desktop=OFF -DFEATURE_icu=OFF
    -DFEATURE_system_zlib=OFF -DFEATURE_system_png=OFF -DFEATURE_system_jpeg=OFF
    -DFEATURE_system_freetype=OFF -DFEATURE_system_harfbuzz=OFF -DFEATURE_system_sqlite=OFF
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ALLPOWERS_QT_JOBS}"
  INSTALL_BYPRODUCTS "${ALLPOWERS_QT_ROOT}/lib/Qt6Core.lib"
  LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON
  STEP_TARGETS configure
  LOG_OUTPUT_ON_FAILURE ON EXCLUDE_FROM_ALL TRUE)
ExternalProject_Add_Step(qtbase-static compiler-config
  DEPENDEES patch DEPENDERS configure
  DEPENDS "${PROJECT_SOURCE_DIR}/cmake/WindowsCompiler.cmake")
ExternalProject_Add(qtconnectivity-static
  URL "${qt_base_url}/qtconnectivity-everywhere-src-${ALLPOWERS_QT_VERSION}.tar.xz"
  URL_HASH "SHA256=${ALLPOWERS_QTCONNECTIVITY_SHA256}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  TIMEOUT 180 INACTIVITY_TIMEOUT 30
  DOWNLOAD_DIR "${ALLPOWERS_QT_SOURCE_CACHE}"
  PREFIX "${PROJECT_BINARY_DIR}/deps/qtconnectivity"
  DEPENDS qtbase-static CMAKE_GENERATOR Ninja
  CMAKE_ARGS ${qt_common} -DFEATURE_winrt_bt=ON "-DCMAKE_PREFIX_PATH=${ALLPOWERS_QT_ROOT}"
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ALLPOWERS_QT_JOBS}"
  INSTALL_BYPRODUCTS "${ALLPOWERS_QT_ROOT}/lib/Qt6Bluetooth.lib"
  LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON
  LOG_OUTPUT_ON_FAILURE ON EXCLUDE_FROM_ALL TRUE)
ExternalProject_Add_Step(qtconnectivity-static compiler-config
  DEPENDEES patch DEPENDERS configure
  DEPENDS "${PROJECT_SOURCE_DIR}/cmake/WindowsCompiler.cmake")
add_custom_target(qt-static)
add_dependencies(qt-static qtconnectivity-static)
