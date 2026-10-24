include(ExternalProject)
ExternalProject_Add(ups-driver-project
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/drivers/windows"
  BINARY_DIR "${PROJECT_BINARY_DIR}/ups-driver"
  DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
  CMAKE_GENERATOR Ninja CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
  BUILD_ALWAYS TRUE INSTALL_COMMAND ""
  LOG_CONFIGURE ON LOG_BUILD ON LOG_OUTPUT_ON_FAILURE ON)
add_custom_target(ups-driver DEPENDS ups-driver-project)
set(ALLPOWERS_DRIVER_PACKAGE_DIR "${PROJECT_BINARY_DIR}/ups-driver/package" CACHE PATH
  "UPS driver payload; VM-signed replacement may be selected for lab builds")
