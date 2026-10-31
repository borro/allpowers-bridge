include("${CMAKE_CURRENT_LIST_DIR}/Arguments.cmake")
# One source inventory for publication checks and source packages.
# Never recursively copy a developer's working directory into a release.
function(ap_check_public_path)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "NAME" "")
  ap_require_arguments(PREFIX AP REQUIRED NAME)
  if(AP_NAME MATCHES "(^|/)(\\.\\.?|\\.local|\\.venv|__pycache__|build|dist|recovery|captures|reports|node_modules)(/|$)" OR
      AP_NAME MATCHES "[;\n\r\\\\]" OR IS_ABSOLUTE "${AP_NAME}")
    message(FATAL_ERROR "Non-public source path: ${AP_NAME}")
  endif()
  if(AP_NAME MATCHES "^(LICENSE|AGENTS\\.md|CONTRIBUTING\\.md|THIRD_PARTY_NOTICES\\.md|README(\\.(ru|zh-CN|zh-TW))?\\.md|CMakeLists\\.txt|CMakePresets\\.json|\\.gitignore|\\.gitattributes|\\.editorconfig|\\.clang-format)$" OR
      AP_NAME MATCHES "^\\.github/(workflows/[a-z-]+\\.yml|dependabot\\.yml)$" OR
      AP_NAME MATCHES "^config/(arduino|devices)\\.example\\.json$" OR
      AP_NAME MATCHES "^(core|firmware|apps|drivers|tests|docs|cmake)/[A-Za-z0-9_./-]+\\.(cpp|h|ino|cmake|md|ps1|py|in)$" OR
      AP_NAME MATCHES "^(core|apps/windows|drivers/windows|tests/native|tests/fuzz)/CMakeLists\\.txt$")
    return()
  endif()
  if(AP_NAME IN_LIST AP_PUBLIC_SPECIAL_FILES)
    return()
  endif()
  # Reviewed synthetic fuzz inputs, stored as text with SHA-256 filenames.
  if(AP_NAME MATCHES "^tests/fuzz/corpus/(ble|hid_wire|hid_server)/[0-9a-f]+\\.hex$")
    get_filename_component(digest "${AP_NAME}" NAME_WE)
    string(LENGTH "${digest}" digest_length)
    if(digest_length EQUAL 64)
      return()
    endif()
  endif()
  message(FATAL_ERROR "File needs explicit publication review: ${AP_NAME}")
endfunction()

set(AP_PUBLIC_SPECIAL_FILES
  tests/cmake/CMakeLists.txt
  tools/windows/CheckExeVersion.ps1
  tools/quality/analysis_controls.py
  tools/quality/static_analysis.py
  tools/quality/coverage_report.py
  tools/quality/mutation_report.py
  tools/ci/ci_policy.py
  tools/ci/ci_summary.py
  tools/ci/dependency_snapshot.py
  tools/release/publish_release.py
  tools/release/firmware_notices.py
  .githooks/pre-commit
  .githooks/pre-push
  .githooks/Host.cmake
  .githooks/Windows.cmake
  .githooks/WindowsEnvironment.cmd
  cmake/projects/HookChecks/CMakeLists.txt
  .clang-tidy
  cmake/modules/quality/AnalysisRules.ruleset
  codecov.yml
  core/src/AllpowersModels.LICENSE.txt
  firmware/R2500Adapter/AllpowersModels.LICENSE.txt
  drivers/windows/LICENSE.MS-PL.txt
  tools/requirements-maintenance.txt
  tools/boards.py
  tools/image_contract.py
  tools/manage_board.py
  firmware/R2500Adapter/partitions.csv
  apps/windows/translations/allpowers_ru.ts
  apps/windows/translations/allpowers_zh_CN.ts
  apps/windows/translations/allpowers_zh_TW.ts
  apps/windows/widget.manifest
  apps/windows/assets/allpowers.ico
  apps/windows/assets/logo.png
  docs/images/windows-monitor-en.png
  docs/images/windows-monitor-ru.png
  docs/images/windows-history-en.png
  docs/images/windows-history-ru.png
  docs/images/windows-widget-en.png
  docs/images/windows-widget-ru.png
  docs/images/windows-monitor-zh-CN.png
  docs/images/windows-monitor-zh-TW.png
  docs/images/windows-history-zh-CN.png
  docs/images/windows-history-zh-TW.png
  docs/images/windows-widget-zh-CN.png
  docs/images/windows-widget-zh-TW.png
  docs/images/waveshare-display.png
  docs/images/pocket-display.png
  tests/fixtures/notifications.json
  tests/mutation/accepted.json
  tests/fixtures/usb-contract.json
  tests/fixtures/hid-telemetry-contract.json)

function(ap_public_sources)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "OUTPUT;ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED OUTPUT ROOT)
  if(EXISTS "${AP_ROOT}/.git")
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -c core.quotepath=false ls-files
      --cached --others --exclude-standard WORKING_DIRECTORY "${AP_ROOT}"
      OUTPUT_VARIABLE inventory COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -c core.quotepath=false ls-files --deleted
      WORKING_DIRECTORY "${AP_ROOT}" OUTPUT_VARIABLE deleted COMMAND_ERROR_IS_FATAL ANY)
  elseif(EXISTS "${AP_ROOT}/SOURCE_FILES")
    file(READ "${AP_ROOT}/SOURCE_FILES" inventory)
  else()
    message(FATAL_ERROR "Source packaging requires Git or the source kit's SOURCE_FILES inventory")
  endif()
  string(REPLACE "\n" ";" files "${inventory}")
  list(FILTER files EXCLUDE REGEX "^$")
  list(REMOVE_DUPLICATES files)
  list(SORT files)
  if(deleted)
    string(REPLACE "\n" ";" deleted "${deleted}")
    list(REMOVE_ITEM files ${deleted})
  endif()
  foreach(name IN LISTS files)
    ap_check_public_path(NAME "${name}")
    if(NOT EXISTS "${AP_ROOT}/${name}" OR IS_DIRECTORY "${AP_ROOT}/${name}" OR IS_SYMLINK "${AP_ROOT}/${name}")
      message(FATAL_ERROR "Public source must be a regular file: ${name}")
    endif()
  endforeach()
  set(${AP_OUTPUT} "${files}" PARENT_SCOPE)
endfunction()
