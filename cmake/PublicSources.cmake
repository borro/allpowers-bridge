# One source inventory for publication checks and source packages.
# Never recursively copy a developer's working directory into a release.
function(ap_check_public_path name)
  if(name MATCHES "(^|/)(\\.\\.?|\\.local|\\.venv|__pycache__|build|dist|recovery|captures|reports|node_modules)(/|$)" OR
      name MATCHES "[;\n\r\\\\]" OR IS_ABSOLUTE "${name}")
    message(FATAL_ERROR "Non-public source path: ${name}")
  endif()
  if(name MATCHES "^(LICENSE|AGENTS\\.md|CONTRIBUTING\\.md|THIRD_PARTY_NOTICES\\.md|README(\\.(ru|zh-CN|zh-TW))?\\.md|CMakeLists\\.txt|CMakePresets\\.json|\\.gitignore|\\.gitattributes|\\.editorconfig|\\.clang-format)$" OR
      name MATCHES "^\\.github/(workflows/[a-z-]+\\.yml|dependabot\\.yml)$" OR
      name MATCHES "^config/(arduino|devices)\\.example\\.json$" OR
      name MATCHES "^(core|firmware|apps|drivers|tests|docs|cmake)/[A-Za-z0-9_./-]+\\.(cpp|h|ino|cmake|md|ps1|py|in)$" OR
      name MATCHES "^(core|apps/windows|drivers/windows|tests/native|tests/fuzz)/CMakeLists\\.txt$")
    return()
  endif()
  if(name IN_LIST AP_PUBLIC_SPECIAL_FILES)
    return()
  endif()
  message(FATAL_ERROR "File needs explicit publication review: ${name}")
endfunction()

set(AP_PUBLIC_SPECIAL_FILES
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
  tests/desktop/frozen_b989/provenance.json
  tests/fixtures/usb-contract.json)

function(ap_public_sources output root)
  if(EXISTS "${root}/.git")
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -c core.quotepath=false ls-files
      --cached --others --exclude-standard WORKING_DIRECTORY "${root}"
      OUTPUT_VARIABLE inventory COMMAND_ERROR_IS_FATAL ANY)
  elseif(EXISTS "${root}/SOURCE_FILES")
    file(READ "${root}/SOURCE_FILES" inventory)
  else()
    message(FATAL_ERROR "Source packaging requires Git or the source kit's SOURCE_FILES inventory")
  endif()
  string(REPLACE "\n" ";" files "${inventory}")
  list(FILTER files EXCLUDE REGEX "^$")
  list(REMOVE_DUPLICATES files)
  list(SORT files)
  foreach(name IN LISTS files)
    ap_check_public_path("${name}")
    if(NOT EXISTS "${root}/${name}" OR IS_DIRECTORY "${root}/${name}" OR IS_SYMLINK "${root}/${name}")
      message(FATAL_ERROR "Public source must be a regular file: ${name}")
    endif()
  endforeach()
  set(${output} "${files}" PARENT_SCOPE)
endfunction()
