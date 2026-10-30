include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

# The workflow starts with an empty build directory; only the host compiler,
# CMake, Python and Git must already be installed. SDKs remain build dependencies.
function(ap_prepare_verification)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  if(NOT CMAKE_MAKE_PROGRAM)
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../sdk/Ninja.cmake")
    ap_setup_ninja(DESTINATION "${AP_BUILD_ROOT}/tools/ninja" OUTPUT ninja)
    set(CMAKE_MAKE_PROGRAM "${ninja}" CACHE FILEPATH "Verified Ninja")
  endif()
  if(CMAKE_HOST_WIN32)
    set(ALLPOWERS_BUILD_WINDOWS ON CACHE BOOL "Build Windows products")
    set(ALLPOWERS_STATIC_ANALYSIS ON CACHE BOOL "Analyze Windows products")
  endif()
endfunction()

# CTest does not build its test executables. This target is the common entry for
# both a single configured mode and the full matrix below.
function(ap_add_check_target)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(dependencies native-tests)
  set(filter)
  if(ALLPOWERS_BUILD_WINDOWS)
    list(APPEND dependencies windows)
  endif()
  if(ALLPOWERS_COVERAGE)
    set(filter -R "^coverage-report$")
  elseif(ALLPOWERS_SANITIZERS)
    set(filter -L "^runtime$")
  elseif(ALLPOWERS_FUZZING)
    set(dependencies fuzz-tests)
    set(filter -L "^fuzz$")
  elseif(ALLPOWERS_MUTATION)
    add_custom_target(check DEPENDS mutation-report)
    return()
  elseif(ALLPOWERS_STATIC_ANALYSIS)
    if(NOT MSVC)
      add_custom_target(check DEPENDS static-analysis)
      return()
    endif()
    list(APPEND dependencies static-analysis)
  endif()
  add_custom_target(check
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${AP_BUILD_ROOT}" ${filter}
      --parallel "${ALLPOWERS_BUILD_JOBS}" --output-on-failure --no-tests=error
      --output-junit "${AP_BUILD_ROOT}/check-tests.xml"
    DEPENDS ${dependencies} USES_TERMINAL VERBATIM)
  if(TARGET valgrind-policy-check)
    find_program(verify_memcheck NAMES valgrind)
    if(NOT verify_memcheck)
      return()
    endif()
    add_custom_target(check-memcheck
      COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${AP_BUILD_ROOT}" -T memcheck -L "^coverage$"
        --parallel "${ALLPOWERS_BUILD_JOBS}" --output-on-failure --no-tests=error --timeout 180
        --overwrite "MemoryCheckCommand=${verify_memcheck}"
        --overwrite "MemoryCheckCommandOptions=--tool=memcheck --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,indirect,possible --track-origins=yes --error-exitcode=99 --num-callers=30"
      DEPENDS native-tests valgrind-policy-check USES_TERMINAL VERBATIM)
  endif()
endfunction()

# Always ask each child's own Ninja graph what changed; do not cache a successful
# test run with a stamp. Presets remain the sole definition of compilation modes.
function(ap_add_check_configuration)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT;PRESET;TARGET" "OPTIONS;DEPENDS")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT PRESET)
  if(NOT AP_TARGET)
    set(AP_TARGET check)
  endif()
  set(binary "${AP_BUILD_ROOT}/${AP_PRESET}")
  add_custom_target("check-${AP_PRESET}"
    COMMAND "${CMAKE_COMMAND}" --preset "${AP_PRESET}" -S "${AP_ROOT}" -B "${binary}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}" "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}"
      "-DALLPOWERS_DEPS_DIR=${ALLPOWERS_DEPS_DIR}" "-DALLPOWERS_RELEASE_DIR=${binary}/dist"
      -DALLPOWERS_BUILD_JOBS=2 ${AP_OPTIONS}
    COMMAND "${CMAKE_COMMAND}" --build "${binary}" --target "${AP_TARGET}" --parallel 2
    WORKING_DIRECTORY "${AP_ROOT}" DEPENDS ${AP_DEPENDS} JOB_POOL verification VERBATIM)
  add_dependencies(check-all "check-${AP_PRESET}")
endfunction()

function(ap_add_verification)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  if(NOT BUILD_TESTING)
    message(FATAL_ERROR "verify requires BUILD_TESTING=ON")
  endif()
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
  set_property(GLOBAL APPEND PROPERTY JOB_POOLS verification=2)
  add_custom_target(maintenance-tests
    COMMAND "${Python3_EXECUTABLE}" -I -S -B -m unittest discover -s tests/maintenance -v
    WORKING_DIRECTORY "${AP_ROOT}" VERBATIM)
  add_custom_target(check-all DEPENDS check maintenance-tests format-check audit-worktree)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_dependencies(check-all workflow-lint)
  endif()
  if(EXISTS "${AP_ROOT}/.git")
    find_package(Git REQUIRED)
    add_custom_target(whitespace-check
      COMMAND "${GIT_EXECUTABLE}" -C "${AP_ROOT}" diff --check
      COMMAND "${GIT_EXECUTABLE}" -C "${AP_ROOT}" diff --cached --check VERBATIM)
    add_dependencies(check-all audit-history audit-secrets whitespace-check)
  else()
    message(STATUS "Unavailable: Git history, staged secrets and whitespace checks (source archive)")
  endif()
  set(coverage_tool "${ALLPOWERS_COVERAGE_TOOL}")
  set(coverage_dependency)
  if(NOT EXISTS "${coverage_tool}")
    add_custom_target(coverage-setup
      COMMAND "${CMAKE_COMMAND}" "-DWORK=${AP_BUILD_ROOT}/tools/coverage"
        -P "${AP_ROOT}/cmake/scripts/sdk/SetupCoverage.cmake" VERBATIM)
    set(coverage_dependency coverage-setup)
    if(WIN32)
      set(coverage_tool "${AP_BUILD_ROOT}/tools/coverage/install/app/OpenCppCoverage.exe")
    else()
      set(coverage_tool "${AP_BUILD_ROOT}/tools/coverage/venv/bin/gcovr")
    endif()
  endif()
  if(WIN32)
    ap_add_check_configuration(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}" PRESET coverage-windows
      DEPENDS ${coverage_dependency} windows
      OPTIONS "-DALLPOWERS_QT_ROOT=${ALLPOWERS_QT_ROOT}"
        "-DALLPOWERS_COVERAGE_TOOL=${coverage_tool}")
    include("${AP_ROOT}/.githooks/Host.cmake")
    ap_add_wsl_verification(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(gcc NAMES "g++-${ALLPOWERS_GCC_MAJOR}")
    find_program(clang NAMES "clang++-${ALLPOWERS_FUZZ_LLVM_VERSION}")
    find_program(ALLPOWERS_CLANG_TIDY NAMES "clang-tidy-${ALLPOWERS_TIDY_LLVM_VERSION}")
    find_program(memcheck NAMES valgrind)
    find_program(dpkg_deb NAMES dpkg-deb)
    if(gcc)
      ap_add_check_configuration(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}" PRESET coverage-linux
        DEPENDS ${coverage_dependency} OPTIONS "-DCMAKE_CXX_COMPILER=${gcc}"
          "-DALLPOWERS_COVERAGE_TOOL=${coverage_tool}")
      ap_add_check_configuration(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}" PRESET sanitizers-linux
        OPTIONS "-DCMAKE_CXX_COMPILER=${gcc}")
      if(memcheck)
        ap_add_check_configuration(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}" PRESET valgrind-linux
          TARGET check-memcheck OPTIONS "-DCMAKE_CXX_COMPILER=${gcc}")
      else()
        message(STATUS "Unavailable: Valgrind (install valgrind)")
      endif()
    else()
      message(STATUS "Unavailable: Linux coverage, sanitizers, Valgrind (install g++-${ALLPOWERS_GCC_MAJOR})")
    endif()
    if(clang)
      ap_add_check_configuration(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}" PRESET fuzz-linux
        OPTIONS "-DCMAKE_CXX_COMPILER=${clang}" -DALLPOWERS_FUZZ_SECONDS=30)
      if(dpkg_deb OR EXISTS "${ALLPOWERS_MULL_ROOT}/bin/mull-runner-${ALLPOWERS_MULL_LLVM_VERSION}")
        set(mull_root "${ALLPOWERS_MULL_ROOT}")
        set(mutation_dependency)
        if(NOT EXISTS "${mull_root}/bin/mull-runner-${ALLPOWERS_MULL_LLVM_VERSION}")
          add_custom_target(mutation-setup
            COMMAND "${CMAKE_COMMAND}" "-DWORK=${AP_BUILD_ROOT}/tools/mull"
              -P "${AP_ROOT}/cmake/scripts/sdk/SetupMutation.cmake" VERBATIM)
          set(mull_root "${AP_BUILD_ROOT}/tools/mull/install/usr")
          set(mutation_dependency mutation-setup)
        endif()
        ap_add_check_configuration(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}" PRESET mutation-linux
          DEPENDS ${mutation_dependency} OPTIONS "-DCMAKE_CXX_COMPILER=${clang}"
            "-DALLPOWERS_MULL_ROOT=${mull_root}")
      else()
        message(STATUS "Unavailable: mutation (install dpkg-deb)")
      endif()
      if(ALLPOWERS_CLANG_TIDY AND EXISTS "${ALLPOWERS_CLANG_TIDY}" AND
          (gcc OR EXISTS "${ALLPOWERS_CPPCHECK_ROOT}/install/bin/cppcheck"))
        set(analysis_options "-DALLPOWERS_CLANG_TIDY=${ALLPOWERS_CLANG_TIDY}")
        if(EXISTS "${ALLPOWERS_CPPCHECK_ROOT}/install/bin/cppcheck")
          list(APPEND analysis_options "-DALLPOWERS_CPPCHECK_ROOT=${ALLPOWERS_CPPCHECK_ROOT}")
        endif()
        ap_add_check_configuration(ROOT "${AP_ROOT}" BUILD_ROOT "${AP_BUILD_ROOT}" PRESET static-analysis-linux
          OPTIONS "-DCMAKE_CXX_COMPILER=${clang}" ${analysis_options})
      else()
        message(STATUS "Unavailable: static analysis (requires clang-tidy-${ALLPOWERS_TIDY_LLVM_VERSION} and GCC ${ALLPOWERS_GCC_MAJOR} or prepared Cppcheck)")
      endif()
    else()
      message(STATUS "Unavailable: fuzz, mutation, static analysis (install clang++-${ALLPOWERS_FUZZ_LLVM_VERSION})")
    endif()
    message(STATUS "Unavailable: Windows products, MSVC analysis and Windows coverage (performed in CI)")
  else()
    message(FATAL_ERROR "verify currently supports Windows and Linux")
  endif()
  add_custom_target(verify DEPENDS products check-all)
  if(TARGET release)
    # Gate source and binary packaging, including its archive rebuild check.
    add_dependencies(source-package check-all products)
    add_dependencies(verify release)
  endif()
endfunction()
