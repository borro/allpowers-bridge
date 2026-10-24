include(ExternalProject)
set(ALLPOWERS_QT_ROOT "${PROJECT_SOURCE_DIR}/build/deps/qt/install" CACHE PATH "Static Qt MSVC installation")
set(ALLPOWERS_QT_SOURCE_CACHE "${PROJECT_SOURCE_DIR}/build/deps/downloads" CACHE PATH "Verified Qt source archives")
set(qt_base_url "https://download.qt.io/archive/qt/6.8/${ALLPOWERS_QT_VERSION}/submodules")
set(qt_common
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
  "-DCMAKE_INSTALL_PREFIX=${ALLPOWERS_QT_ROOT}")
if(NOT EXISTS "${ALLPOWERS_QT_ROOT}/lib/cmake/Qt6Bluetooth/Qt6BluetoothConfig.cmake")
  ExternalProject_Add(qtbase-static
    URL "${qt_base_url}/qtbase-everywhere-src-${ALLPOWERS_QT_VERSION}.tar.xz"
    URL_HASH "SHA256=${ALLPOWERS_QTBASE_SHA256}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    TIMEOUT 180 INACTIVITY_TIMEOUT 30
    DOWNLOAD_DIR "${ALLPOWERS_QT_SOURCE_CACHE}"
    PREFIX "${PROJECT_BINARY_DIR}/deps/qtbase"
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS ${qt_common} -DFEATURE_static_runtime=ON -DFEATURE_opengl=OFF
      -DINPUT_opengl=no -DFEATURE_opengl_dynamic=OFF -DFEATURE_opengl_desktop=OFF -DFEATURE_icu=OFF
      -DFEATURE_system_zlib=OFF -DFEATURE_system_png=OFF -DFEATURE_system_jpeg=OFF
      -DFEATURE_system_freetype=OFF -DFEATURE_system_harfbuzz=OFF -DFEATURE_system_sqlite=OFF
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel 8
    BUILD_BYPRODUCTS "${ALLPOWERS_QT_ROOT}/lib/Qt6Core.lib"
    LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON
    STEP_TARGETS configure
    LOG_OUTPUT_ON_FAILURE ON EXCLUDE_FROM_ALL TRUE)
  ExternalProject_Add(qtconnectivity-static
    URL "${qt_base_url}/qtconnectivity-everywhere-src-${ALLPOWERS_QT_VERSION}.tar.xz"
    URL_HASH "SHA256=${ALLPOWERS_QTCONNECTIVITY_SHA256}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    TIMEOUT 180 INACTIVITY_TIMEOUT 30
    DOWNLOAD_DIR "${ALLPOWERS_QT_SOURCE_CACHE}"
    PREFIX "${PROJECT_BINARY_DIR}/deps/qtconnectivity"
    DEPENDS qtbase-static CMAKE_GENERATOR Ninja
    CMAKE_ARGS ${qt_common} "-DCMAKE_PREFIX_PATH=${ALLPOWERS_QT_ROOT}"
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel 8
    BUILD_BYPRODUCTS "${ALLPOWERS_QT_ROOT}/lib/Qt6Bluetooth.lib"
    LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON EXCLUDE_FROM_ALL TRUE)
  add_custom_target(qt-static DEPENDS qtconnectivity-static)
else()
  add_custom_target(qt-static)
endif()
