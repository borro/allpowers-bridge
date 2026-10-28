include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")

function(ap_firmware_source_notice output tag commit)
  set(repository "https://github.com/borro/allpowers-bridge")
  set(text "# Firmware sources for ${tag}\n\n")
  string(APPEND text "Project source: [${tag}](${repository}/tree/${commit}).\n"
    "GitHub source archive: ${repository}/archive/${commit}.zip\n"
    "Build commit: `${commit}`.\n\n"
    "## Rebuild\n\n"
    "For the supported build, clone the exact release tag (Git supplies version and source inventory metadata):\n\n"
    "```sh\ngit clone --branch ${tag} --depth 1 ${repository}.git allpowers\n"
    "cd allpowers\ncmake --preset firmware-linux\ncmake --build --preset firmware-linux\n```\n\n"
    "Use the pinned tools and prerequisites in [the build guide](${repository}/blob/${commit}/docs/building.md). "
    "The build downloads the pinned SDK and dependencies; they are not inside GitHub's repository archive. "
    "Builds never flash a board or connect to a station.\n\n"
    "For modified LGPL libraries and installation instructions see "
    "[firmware source and license guide](${repository}/blob/${commit}/docs/firmware-licenses.md). "
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
  string(APPEND text "\nThe archive's `licenses.zip` retains license texts and notices. "
    "Generated object files and the internal relinking test kit are not firmware installation files.\n")
  file(WRITE "${output}" "${text}")
endfunction()
