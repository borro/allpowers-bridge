include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../Dependencies.cmake")

function(ap_firmware_source_notice)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "OUTPUT;TAG;COMMIT" "")
  ap_require_arguments(PREFIX AP REQUIRED OUTPUT TAG COMMIT)
  set(repository "https://github.com/borro/allpowers-bridge")
  set(text "# Firmware sources for ${AP_TAG}\n\n")
  string(APPEND text "Project source: [${AP_TAG}](${repository}/tree/${AP_COMMIT}).\n"
    "GitHub source archive: ${repository}/archive/${AP_COMMIT}.zip\n"
    "Build commit: `${AP_COMMIT}`.\n\n"
    "## Rebuild\n\n"
    "For the supported build, clone the exact release tag (Git supplies version and source inventory metadata):\n\n"
    "```sh\ngit clone --branch ${AP_TAG} --depth 1 ${repository}.git allpowers\n"
    "cd allpowers\ncmake --preset firmware-linux\ncmake --build --preset firmware-linux\n```\n\n"
    "Use the pinned tools and prerequisites in [the build guide](${repository}/blob/${AP_COMMIT}/docs/building.md). "
    "The build downloads the pinned SDK and dependencies; they are not inside GitHub's repository archive. "
    "Builds never flash a board or connect to a station.\n\n"
    "For modified LGPL libraries and installation instructions see "
    "[firmware source and license guide](${repository}/blob/${AP_COMMIT}/docs/firmware-licenses.md). "
    "The source ZIP alone has no Git version/inventory metadata; use the clone command above.\n\n"
    "## Exact dependency sources and license evidence\n\n"
    "These are the source versions corresponding to the pinned SDK, not upstream main branches. "
    "The SDK also contains prebuilt binary components; their license-source references are listed separately.\n\n")
  foreach(record IN LISTS ALLPOWERS_FIRMWARE_SOURCE_ARCHIVES ALLPOWERS_FIRMWARE_BINARY_LICENSES)
    string(REPLACE "|" ";" fields "${record}")
    list(GET fields 0 name)
    list(GET fields 1 url)
    list(GET fields 2 hash)
    string(APPEND text "- [${name}](${url})\n  SHA256: `${hash}`\n")
  endforeach()
  string(APPEND text "\nThe archive's `licenses/README.md` maps linked components to unique full license texts and copyright/NOTICE records in `licenses/NOTICES.md`. "
    "Generated object files and the internal relinking test kit are not firmware installation files.\n")
  file(WRITE "${AP_OUTPUT}" "${text}")
endfunction()
