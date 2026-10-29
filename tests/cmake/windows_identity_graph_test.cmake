cmake_minimum_required(VERSION 4.4)
find_package(Git REQUIRED)
find_program(powershell NAMES pwsh powershell REQUIRED)
include("${ROOT}/cmake/modules/project/BuildParallelism.cmake")
ap_configure_parallelism()
foreach(required WORK BUILD_ROOT QT_ROOT SOURCE_CACHE DEPS_DIR CXX)
  if(NOT ${required})
    message(FATAL_ERROR "Set ${required} for the real Windows identity graph regression")
  endif()
endforeach()
file(MAKE_DIRECTORY "${WORK}")
file(REAL_PATH "${WORK}" scratch)
file(REAL_PATH "${BUILD_ROOT}" build_root)
cmake_path(IS_PREFIX build_root "${scratch}" NORMALIZE inside)
if(NOT inside OR scratch STREQUAL build_root)
  message(FATAL_ERROR "Windows identity test needs a dedicated binary subdirectory")
endif()
file(REMOVE_RECURSE "${scratch}")
function(timed_step label)
  string(TIMESTAMP started "%s")
  execute_process(${ARGN} RESULT_VARIABLE result)
  string(TIMESTAMP finished "%s")
  math(EXPR seconds "${finished}-${started}")
  message(STATUS "Windows identity ${label}: ${seconds}s (result ${result})")
  file(APPEND "${scratch}/timings.log" "${label}: ${seconds}s (result ${result})\n")
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Windows identity ${label} failed; see ${scratch}/${label}.log")
  endif()
endfunction()
set(source "${scratch}/source")
file(MAKE_DIRECTORY "${source}")
if(SNAPSHOT_REF)
  # Used to retain evidence that the same regression fails before the fix.
  execute_process(COMMAND "${GIT_EXECUTABLE}" archive --format=tar "--output=${scratch}/snapshot.tar" "${SNAPSHOT_REF}"
    WORKING_DIRECTORY "${ROOT}" COMMAND_ERROR_IS_FATAL ANY)
  file(ARCHIVE_EXTRACT INPUT "${scratch}/snapshot.tar" DESTINATION "${source}")
else()
  include("${ROOT}/cmake/modules/common/PublicSources.cmake")
  ap_public_sources(OUTPUT files ROOT "${ROOT}")
  foreach(name IN LISTS files)
    get_filename_component(parent "${name}" DIRECTORY)
    file(MAKE_DIRECTORY "${source}/${parent}")
    file(COPY_FILE "${ROOT}/${name}" "${source}/${name}")
  endforeach()
endif()
function(git)
  execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGN} WORKING_DIRECTORY "${source}"
    OUTPUT_QUIET ERROR_QUIET COMMAND_ERROR_IS_FATAL ANY)
endfunction()
git(init -q)
git(config user.name "Windows Identity Graph Test")
git(config user.email "identity@example.invalid")
git(config commit.gpgsign false)
git(add .)
git(commit -qm fixture)
git(tag v1.2.3)
set(common -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
  "-DCMAKE_CXX_COMPILER=${CXX}" -DBUILD_TESTING=ON)
timed_step(root-configure COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${source}/build/root" ${common}
  -DALLPOWERS_BUILD_WINDOWS=ON "-DALLPOWERS_BUILD_JOBS=${ALLPOWERS_BUILD_JOBS}" "-DALLPOWERS_QT_ROOT=${QT_ROOT}"
  "-DALLPOWERS_QT_SOURCE_CACHE=${SOURCE_CACHE}" "-DALLPOWERS_DEPS_DIR=${DEPS_DIR}"
  OUTPUT_FILE "${scratch}/root-configure.log" ERROR_FILE "${scratch}/root-configure.log")
timed_step(standalone-configure COMMAND "${CMAKE_COMMAND}" -S "${source}/apps/windows" -B "${source}/build/standalone" ${common}
  "-DCMAKE_PREFIX_PATH=${QT_ROOT}" "-DALLPOWERS_QT_SOURCE_CACHE=${SOURCE_CACHE}"
  OUTPUT_FILE "${scratch}/standalone-configure.log" ERROR_FILE "${scratch}/standalone-configure.log")
function(build phase)
  timed_step("${phase}-root-build" COMMAND "${CMAKE_COMMAND}" --build "${source}/build/root"
    --target windows --parallel "${ALLPOWERS_BUILD_JOBS}"
    OUTPUT_FILE "${scratch}/${phase}-root-build.log" ERROR_FILE "${scratch}/${phase}-root-build.log")
  timed_step("${phase}-standalone-build" COMMAND "${CMAKE_COMMAND}" --build "${source}/build/standalone"
    --target AllpowersMonitor AllpowersWidget
    --parallel "${ALLPOWERS_BUILD_JOBS}"
    OUTPUT_FILE "${scratch}/${phase}-standalone-build.log" ERROR_FILE "${scratch}/${phase}-standalone-build.log")
endfunction()
function(check expected phase)
  foreach(layout root standalone)
    string(TIMESTAMP started "%s")
    if(layout STREQUAL root)
      set(folder "${source}/build/root/dist/windows/AllpowersMonitor")
    else()
      set(folder "${source}/build/standalone")
    endif()
    foreach(app AllpowersMonitor AllpowersWidget)
      execute_process(COMMAND "${folder}/${app}.exe" --version -platform offscreen
        OUTPUT_VARIABLE actual OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_VARIABLE error COMMAND_ERROR_IS_FATAL ANY)
      if(NOT actual STREQUAL "${app} ${expected}")
        message(FATAL_ERROR "${layout}/${app} CLI version '${actual}' != '${expected}': ${error}")
      endif()
      execute_process(COMMAND "${powershell}" -NoProfile -File "${ROOT}/tools/windows/CheckExeVersion.ps1"
        -Path "${folder}/${app}.exe" -ExpectedVersion "${expected}" OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
    endforeach()
    execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${source}/build/${layout}"
      -R "^(single-exe|widget-single-exe)$" --output-on-failure --parallel 2 --no-tests=error
      OUTPUT_VARIABLE tests ERROR_VARIABLE error RESULT_VARIABLE result)
    file(WRITE "${scratch}/${phase}-${layout}-ctest.log" "${tests}\n${error}")
    if(result)
      message(FATAL_ERROR "${phase} ${layout} CTest rejected current CLI/PE identity: ${tests}\n${error}")
    endif()
    string(TIMESTAMP finished "%s")
    math(EXPR seconds "${finished}-${started}")
    message(STATUS "Windows identity ${phase}-${layout}-checks: ${seconds}s")
    file(APPEND "${scratch}/timings.log" "${phase}-${layout}-checks: ${seconds}s\n")
  endforeach()
endfunction()
function(unchanged_objects mode)
  foreach(layout root standalone)
    set(binary "${source}/build/${layout}")
    if(layout STREQUAL root)
      string(APPEND binary /windows-monitor)
    endif()
    foreach(object monitor_backend.dir/session.cpp.obj widget_lib.dir/widget.cpp.obj)
      set(path "${binary}/CMakeFiles/${object}")
      if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing dependency regression object: ${path}")
      endif()
      string(MAKE_C_IDENTIFIER "${layout}_${object}" key)
      file(TIMESTAMP "${path}" timestamp "%s")
      if(mode STREQUAL record)
        set_property(GLOBAL PROPERTY "unchanged_${key}" "${timestamp}")
      else()
        get_property(previous GLOBAL PROPERTY "unchanged_${key}")
        if(NOT timestamp STREQUAL previous)
          message(FATAL_ERROR "Version-only identity change recompiled ${layout}/${object}")
        endif()
      endif()
    endforeach()
  endforeach()
endfunction()
build(clean)
check(1.2.3 clean)
unchanged_objects(record)
# An ordinary source-only edit must not require manual configure and must update
# Monitor and Widget CLI, PE resources, and expected values in both CTest graphs.
file(APPEND "${source}/apps/windows/main.cpp" "\n// source-only identity regression\n")
build(dirty)
check(1.2.3-dirty dirty)
unchanged_objects(check)
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${source}/build/root/check-package-identity.cmake"
  RESULT_VARIABLE packaged OUTPUT_QUIET ERROR_QUIET)
if(packaged EQUAL 0)
  message(FATAL_ERROR "Dirty incremental build bypassed the configured clean package identity")
endif()
git(checkout -- apps/windows/main.cpp)
build(reverted)
check(1.2.3 reverted)
unchanged_objects(check)
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${source}/build/root/check-package-identity.cmake"
  OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
# A caller supplying a wrong identity must still reject a valid executable.
file(WRITE "${scratch}/wrong-identity.cmake" "set(ALLPOWERS_VERSION \"9.8.7\")\n")
execute_process(COMMAND "${CMAKE_COMMAND}"
  "-DEXE=${source}/build/standalone/AllpowersWidget.exe" "-DIDENTITY_FILE=${scratch}/wrong-identity.cmake"
  "-DWORK=${scratch}/wrong-exe" -DWIDGET=ON -P "${ROOT}/cmake/scripts/quality/CheckSingleExe.cmake"
  RESULT_VARIABLE accepted OUTPUT_QUIET ERROR_QUIET)
if(accepted EQUAL 0)
  message(FATAL_ERROR "A wrong generated expected identity accepted the executable")
endif()
# The driver producer runs on every root build, but an unchanged producer must
# not force another monitor configure. Ninja still owns genuine child inputs.
set(root_binary "${source}/build/root")
set(monitor_binary "${root_binary}/windows-monitor")
set(configure_stamp "${root_binary}/windows-monitor-prefix/src/windows-monitor-stamp/windows-monitor-configure")
file(TIMESTAMP "${configure_stamp}" configured_before "%s.%f")
timed_step(warm-root-build COMMAND "${CMAKE_COMMAND}" --build "${root_binary}"
  --target windows --parallel "${ALLPOWERS_BUILD_JOBS}"
  OUTPUT_FILE "${scratch}/warm-root-build.log" ERROR_FILE "${scratch}/warm-root-build.log")
file(TIMESTAMP "${configure_stamp}" configured_after "%s.%f")
if(NOT configured_before OR NOT configured_before STREQUAL configured_after)
  message(FATAL_ERROR "Unchanged external dependency forced monitor configure")
endif()

# Use only copies inside this synthetic checkout: never modify or install a
# developer's driver package. Changing CMAKE_ARGS must still configure the child.
set(driver_copy "${scratch}/alternate-driver")
file(MAKE_DIRECTORY "${driver_copy}")
foreach(name AllpowersUps.sys AllpowersUps.inf AllpowersUps.cat)
  file(COPY_FILE "${root_binary}/ups-driver/package/${name}" "${driver_copy}/${name}")
endforeach()
timed_step(driver-path-configure COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${root_binary}"
  "-DALLPOWERS_DRIVER_PACKAGE_DIR=${driver_copy}"
  OUTPUT_FILE "${scratch}/driver-path-configure.log" ERROR_FILE "${scratch}/driver-path-configure.log")
timed_step(driver-path-build COMMAND "${CMAKE_COMMAND}" --build "${root_binary}"
  --target windows --parallel "${ALLPOWERS_BUILD_JOBS}"
  OUTPUT_FILE "${scratch}/driver-path-build.log" ERROR_FILE "${scratch}/driver-path-build.log")
file(TIMESTAMP "${configure_stamp}" configured_path "%s.%f")
if(configured_path STREQUAL configured_after)
  message(FATAL_ERROR "Changed external CMAKE_ARGS did not configure the monitor")
endif()

# A payload-only change must reach the embedded resource without forcing that
# configure step. CheckSingleExe compares the extracted bytes to this copy.
file(APPEND "${driver_copy}/AllpowersUps.inf" "\n; Synthetic embedded-payload dependency probe\n")
timed_step(driver-payload-build COMMAND "${CMAKE_COMMAND}" --build "${root_binary}"
  --target windows --parallel "${ALLPOWERS_BUILD_JOBS}"
  OUTPUT_FILE "${scratch}/driver-payload-build.log" ERROR_FILE "${scratch}/driver-payload-build.log")
file(TIMESTAMP "${configure_stamp}" configured_payload "%s.%f")
if(NOT configured_payload STREQUAL configured_path)
  message(FATAL_ERROR "Payload-only change forced the external configure step")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DEXE=${monitor_binary}/AllpowersMonitor.exe"
  "-DIDENTITY_FILE=${root_binary}/identity/product-identity.cmake"
  "-DDRIVER_PACKAGE=${driver_copy}" "-DWORK=${scratch}/changed-payload-check"
  -P "${ROOT}/cmake/scripts/quality/CheckSingleExe.cmake" COMMAND_ERROR_IS_FATAL ANY)

file(APPEND "${source}/apps/windows/CMakeLists.txt" [=[

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/child-configure-probe.txt" "observed")
]=])
timed_step(child-cmake-build COMMAND "${CMAKE_COMMAND}" --build "${root_binary}"
  --target windows --parallel "${ALLPOWERS_BUILD_JOBS}"
  OUTPUT_FILE "${scratch}/child-cmake-build.log" ERROR_FILE "${scratch}/child-cmake-build.log")
if(NOT EXISTS "${monitor_binary}/child-configure-probe.txt")
  message(FATAL_ERROR "Child CMake source change did not regenerate its build graph")
endif()
message(STATUS "Root and standalone Monitor/Widget CLI, PE and CTest pass clean/source-only-dirty/reverted identity without manual configure")
