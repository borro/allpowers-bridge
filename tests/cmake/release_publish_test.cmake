cmake_minimum_required(VERSION 4.4)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
string(RANDOM LENGTH 24 ALPHABET 0123456789abcdef suffix)
set(scratch "${WORK}/${suffix}")
set(assets "${scratch}/assets")
file(MAKE_DIRECTORY "${assets}")
# Synthetic bytes only: publication tests do not build or upload products.
set(version "1.2.3-alpha.7_build.01")
foreach(name "AllpowersMonitor-${version}-sources-with-qt.zip"
    "AllpowersMonitor-${version}-win-x64.exe" "AllpowersWidget-${version}-win-x64.exe"
    "AllpowersFirmware-${version}-waveshare-147.zip" "AllpowersFirmware-${version}-pocket-096.zip")
  file(WRITE "${assets}/${name}" "Synthetic release asset: ${name}\n")
  file(SHA256 "${assets}/${name}" hash)
  file(APPEND "${assets}/SHA256SUMS" "${hash}  ${name}\n")
endforeach()
execute_process(COMMAND "${Python3_EXECUTABLE}" -I -S -B "${ROOT}/tests/cmake/release_publish_test.py"
  --root "${ROOT}" --work "${scratch}/publisher" --assets "${assets}" --tag "v1.2.3-alpha.7+build.01"
  COMMAND_ERROR_IS_FATAL ANY)
