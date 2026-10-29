include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

function(ap_add_qt_sdk)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(PROJECT_SOURCE_DIR "${AP_ROOT}")
  set(PROJECT_BINARY_DIR "${AP_BUILD_ROOT}")
  include(ExternalProject)
  option(ALLPOWERS_QT_LOG_BUILD "Capture Qt compilation in ExternalProject logs" ON)
  if(ALLPOWERS_QT_LOG_BUILD)
    set(qt_build_terminal FALSE)
  else()
    # Ninja buffers ordinary jobs. Its console pool streams the inner build live.
    set(qt_build_terminal TRUE)
  endif()
  set(ALLPOWERS_QT_JOBS 2 CACHE STRING "Parallel Qt compiler jobs (bounded for hosted CI runners)")
  set(ALLPOWERS_QT_ROOT "" CACHE PATH "Optional prebuilt static Qt MSVC installation")
  set(ALLPOWERS_QT_SOURCE_CACHE "${ALLPOWERS_DEPS_DIR}/downloads" CACHE PATH "Verified Qt source archives")
  if(ALLPOWERS_QT_ROOT)
    if(NOT EXISTS "${ALLPOWERS_QT_ROOT}/lib/cmake/Qt6Bluetooth/Qt6BluetoothConfig.cmake")
      message(FATAL_ERROR "ALLPOWERS_QT_ROOT must point to a complete static Qt installation")
    endif()
    if(NOT EXISTS "${ALLPOWERS_QT_ROOT}/lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake")
      message(FATAL_ERROR "The static Qt SDK must include matching LinguistTools (lrelease/lupdate)")
    endif()
    add_custom_target(qt-static)
    return(PROPAGATE ALLPOWERS_QT_ROOT)
  endif()
  # Managed installs belong to this build tree and keep their producer targets
  # even after installation. An existing config file must not disable the graph.
  set(ALLPOWERS_QT_ROOT "${PROJECT_BINARY_DIR}/deps/qt/install")
  set(qt_common
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
    -DFEATURE_optimize_size=ON -DFEATURE_optimize_full=OFF -DFEATURE_ltcg=OFF
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
    "-DCMAKE_CXX_STANDARD=${ALLPOWERS_CXX_STANDARD}" -DCMAKE_CXX_STANDARD_REQUIRED=ON
    -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    "-DCMAKE_PROJECT_INCLUDE=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../scripts/sdk/QtCompiler.cmake"
    "-DCMAKE_INSTALL_PREFIX=${ALLPOWERS_QT_ROOT}")
  ExternalProject_Add(qtbase-static
    DOWNLOAD_COMMAND "${CMAKE_COMMAND}" -DMODULE=qtbase
      "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
      -DOUTPUT=<SOURCE_DIR> -P "${PROJECT_SOURCE_DIR}/cmake/scripts/sdk/ExtractQtSource.cmake"
    PREFIX "${PROJECT_BINARY_DIR}/deps/qtbase"
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS ${qt_common} -DFEATURE_cpp_winrt=ON -DFEATURE_static_runtime=ON -DFEATURE_opengl=OFF
      -DFEATURE_dbus=OFF -DFEATURE_printsupport=OFF -DFEATURE_concurrent=OFF
      -DFEATURE_xml=OFF -DFEATURE_sql_odbc=OFF -DFEATURE_qmake=OFF
      -DFEATURE_androiddeployqt=OFF -DFEATURE_harmonydeployqt=OFF
      -DFEATURE_wasmdeployqt=OFF -DFEATURE_windeployqt=OFF
      -DINPUT_opengl=no -DFEATURE_opengl_dynamic=OFF -DFEATURE_opengl_desktop=OFF -DFEATURE_icu=OFF
      -DFEATURE_system_zlib=OFF -DFEATURE_system_png=OFF -DFEATURE_system_jpeg=OFF
      -DFEATURE_system_freetype=OFF -DFEATURE_system_harfbuzz=OFF -DFEATURE_system_sqlite=OFF
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ALLPOWERS_QT_JOBS}"
    INSTALL_BYPRODUCTS "${ALLPOWERS_QT_ROOT}/lib/Qt6Core.lib"
    LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ${ALLPOWERS_QT_LOG_BUILD} LOG_INSTALL ON
    USES_TERMINAL_BUILD ${qt_build_terminal}
    STEP_TARGETS configure
    LOG_OUTPUT_ON_FAILURE ON EXCLUDE_FROM_ALL TRUE)
  ExternalProject_Add_Step(qtbase-static compiler-config
    DEPENDEES patch DEPENDERS configure
    DEPENDS "${PROJECT_SOURCE_DIR}/cmake/scripts/sdk/QtCompiler.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/modules/project/WindowsCompiler.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/modules/common/Arguments.cmake")
  ExternalProject_Add(qtconnectivity-static
    DOWNLOAD_COMMAND "${CMAKE_COMMAND}" -DMODULE=qtconnectivity
      "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
      -DOUTPUT=<SOURCE_DIR> -P "${PROJECT_SOURCE_DIR}/cmake/scripts/sdk/ExtractQtSource.cmake"
    PREFIX "${PROJECT_BINARY_DIR}/deps/qtconnectivity"
    DEPENDS qtbase-static CMAKE_GENERATOR Ninja
    CMAKE_ARGS ${qt_common} -DFEATURE_winrt_bt=ON -DFEATURE_nfc=OFF "-DCMAKE_PREFIX_PATH=${ALLPOWERS_QT_ROOT}"
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ALLPOWERS_QT_JOBS}"
    INSTALL_BYPRODUCTS "${ALLPOWERS_QT_ROOT}/lib/Qt6Bluetooth.lib"
    LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ${ALLPOWERS_QT_LOG_BUILD} LOG_INSTALL ON
    USES_TERMINAL_BUILD ${qt_build_terminal}
    LOG_OUTPUT_ON_FAILURE ON EXCLUDE_FROM_ALL TRUE)
  ExternalProject_Add_Step(qtconnectivity-static compiler-config
    DEPENDEES patch DEPENDERS configure
    DEPENDS "${PROJECT_SOURCE_DIR}/cmake/scripts/sdk/QtCompiler.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/modules/project/WindowsCompiler.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/modules/common/Arguments.cmake")
  # Verify on every use, but seal only after installation. Automatically resealing
  # on every build would silently accept libraries changed since the last build.
  ExternalProject_Add(qttools-static
    DOWNLOAD_COMMAND "${CMAKE_COMMAND}" -DMODULE=qttools
      "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}" "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
      -DOUTPUT=<SOURCE_DIR> -P "${PROJECT_SOURCE_DIR}/cmake/scripts/sdk/ExtractQtSource.cmake"
    PREFIX "${PROJECT_BINARY_DIR}/deps/qttools"
    DEPENDS qtconnectivity-static CMAKE_GENERATOR Ninja
    CMAKE_ARGS ${qt_common} "-DCMAKE_PREFIX_PATH=${ALLPOWERS_QT_ROOT}"
      -DFEATURE_assistant=OFF -DFEATURE_designer=OFF -DFEATURE_qdoc=OFF
      -DFEATURE_distancefieldgenerator=OFF -DFEATURE_kmap2qmap=OFF -DFEATURE_pixeltool=OFF
      -DFEATURE_qdbus=OFF -DFEATURE_qev=OFF -DFEATURE_qtattributionsscanner=OFF
      -DFEATURE_qtdiag=OFF -DFEATURE_qtplugininfo=OFF
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${ALLPOWERS_QT_JOBS}"
    INSTALL_BYPRODUCTS "${ALLPOWERS_QT_ROOT}/bin/lrelease.exe" "${ALLPOWERS_QT_ROOT}/bin/lupdate.exe"
    LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ${ALLPOWERS_QT_LOG_BUILD} LOG_INSTALL ON
    USES_TERMINAL_BUILD ${qt_build_terminal}
    LOG_OUTPUT_ON_FAILURE ON EXCLUDE_FROM_ALL TRUE)
  ExternalProject_Add_Step(qttools-static compiler-config
    DEPENDEES patch DEPENDERS configure
    DEPENDS "${PROJECT_SOURCE_DIR}/cmake/scripts/sdk/QtCompiler.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/modules/project/WindowsCompiler.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/modules/common/Arguments.cmake")
  ExternalProject_Add_Step(qttools-static source-identity
    COMMAND "${CMAKE_COMMAND}" -DMODE=SEAL "-DPREFIX=${ALLPOWERS_QT_ROOT}"
      "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}"
      "-DWORK=${PROJECT_BINARY_DIR}/qt-source-identity"
      "-Dqtbase_SOURCE=${PROJECT_BINARY_DIR}/deps/qtbase/src/qtbase-static"
      "-Dqttools_SOURCE=${PROJECT_BINARY_DIR}/deps/qttools/src/qttools-static"
      "-Dqtconnectivity_SOURCE=${PROJECT_BINARY_DIR}/deps/qtconnectivity/src/qtconnectivity-static"
      -P "${PROJECT_SOURCE_DIR}/cmake/scripts/sdk/QtSourceIdentity.cmake"
    DEPENDEES install
    DEPENDS "${PROJECT_SOURCE_DIR}/cmake/scripts/sdk/QtSourceIdentity.cmake"
    BYPRODUCTS "${ALLPOWERS_QT_ROOT}/allpowers-source-identity.cmake")
  add_custom_target(qt-static
    COMMAND "${CMAKE_COMMAND}" -DMODE=VERIFY "-DPREFIX=${ALLPOWERS_QT_ROOT}"
      "-DSOURCE_CACHE=${ALLPOWERS_QT_SOURCE_CACHE}"
      -P "${PROJECT_SOURCE_DIR}/cmake/scripts/sdk/QtSourceIdentity.cmake" VERBATIM)
  add_dependencies(qt-static qttools-static)
  return(PROPAGATE ALLPOWERS_QT_ROOT)
endfunction()
