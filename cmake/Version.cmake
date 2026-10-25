if(NOT ALLPOWERS_ROOT)
  get_filename_component(ALLPOWERS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
find_package(Git QUIET)
set(ALLPOWERS_RELEASE_TAG "" CACHE STRING "Optional exact Git tag required for publication")
set(ALLPOWERS_RELEASE_COMMIT "" CACHE STRING "Optional Git object that triggered a tagged build")
set(release_commit "${ALLPOWERS_RELEASE_COMMIT}")
if(ALLPOWERS_RELEASE_TAG AND NOT (GIT_FOUND AND EXISTS "${ALLPOWERS_ROOT}/.git"))
  message(FATAL_ERROR "Publishing requires the actual Git checkout and tag")
endif()
set(ALLPOWERS_VERSION "")
if(GIT_FOUND AND EXISTS "${ALLPOWERS_ROOT}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*" --dirty
    WORKING_DIRECTORY "${ALLPOWERS_ROOT}" OUTPUT_VARIABLE ALLPOWERS_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  # git describe --dirty omits untracked files. New source inputs are already
  # part of the build inventory and must not impersonate a clean tagged build.
  execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
    WORKING_DIRECTORY "${ALLPOWERS_ROOT}" OUTPUT_VARIABLE version_changes
    COMMAND_ERROR_IS_FATAL ANY)
  if(ALLPOWERS_RELEASE_TAG)
    include("${CMAKE_CURRENT_LIST_DIR}/ReleaseVersion.cmake")
    ap_check_release_tag("${ALLPOWERS_ROOT}" "${ALLPOWERS_RELEASE_TAG}" ALLPOWERS_VERSION "${release_commit}")
  endif()
  execute_process(COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
    WORKING_DIRECTORY "${ALLPOWERS_ROOT}" OUTPUT_VARIABLE branch
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  # Resolve through Git so linked worktrees and packed refs behave like clones.
  foreach(ref HEAD packed-refs refs/tags "${branch}")
    if(NOT ref)
      continue()
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --git-path "${ref}"
      WORKING_DIRECTORY "${ALLPOWERS_ROOT}" OUTPUT_VARIABLE path
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    get_filename_component(path "${path}" ABSOLUTE BASE_DIR "${ALLPOWERS_ROOT}")
    if(EXISTS "${path}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    endif()
  endforeach()
elseif(EXISTS "${ALLPOWERS_ROOT}/VERSION")
  if(ALLPOWERS_RELEASE_TAG)
    message(FATAL_ERROR "Publishing requires the actual Git tag, not a source archive")
  endif()
  file(READ "${ALLPOWERS_ROOT}/VERSION" ALLPOWERS_VERSION)
  string(STRIP "${ALLPOWERS_VERSION}" ALLPOWERS_VERSION)
endif()
if(NOT ALLPOWERS_VERSION)
  set(ALLPOWERS_VERSION "0.0.0-untagged")
endif()
string(REGEX REPLACE "^v" "" ALLPOWERS_VERSION "${ALLPOWERS_VERSION}")
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" ALLPOWERS_VERSION_NUMERIC "${ALLPOWERS_VERSION}")
if(NOT ALLPOWERS_VERSION_NUMERIC)
  message(FATAL_ERROR "Invalid product tag/version: ${ALLPOWERS_VERSION}")
endif()
if(ALLPOWERS_PARENT_IDENTITY)
  # The root is authoritative, including an explicitly chosen tag when several
  # tags name HEAD. A child must never choose a competing git-describe result.
  include("${ALLPOWERS_PARENT_IDENTITY}")
  if(GIT_FOUND AND EXISTS "${ALLPOWERS_ROOT}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${ALLPOWERS_ROOT}"
      OUTPUT_VARIABLE current_commit OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT ALLPOWERS_BUILD_COMMIT STREQUAL current_commit)
      message(FATAL_ERROR "Parent build identity belongs to another commit; build the root project first")
    endif()
    # A direct child rebuild may occur without refreshing the parent's file.
    # Retain its selected tag/base, but derive dirtiness from today's inputs.
    string(REGEX REPLACE "-dirty$" "" ALLPOWERS_VERSION "${ALLPOWERS_VERSION}")
  endif()
endif()
if(GIT_FOUND AND EXISTS "${ALLPOWERS_ROOT}/.git" AND version_changes AND
    NOT ALLPOWERS_VERSION MATCHES "-dirty$")
  string(APPEND ALLPOWERS_VERSION "-dirty")
endif()
if(NOT CMAKE_SCRIPT_MODE_FILE)
  message(STATUS "ALLPOWERS version: ${ALLPOWERS_VERSION}")
endif()

function(ap_add_build_identity)
  set(directory "${CMAKE_BINARY_DIR}/identity")
  set(ALLPOWERS_IDENTITY_FILE "${directory}/product-identity.cmake" PARENT_SCOPE)
  set(ALLPOWERS_IDENTITY_HEADER "${directory}/product-version.h" PARENT_SCOPE)
  # Supply initial files for generators/scanners. The same operation runs on
  # every build and rewrites only files whose contents actually changed.
  set(arguments "-DALLPOWERS_ROOT=${ALLPOWERS_ROOT}"
    "-DALLPOWERS_RELEASE_TAG=${ALLPOWERS_RELEASE_TAG}"
    "-DALLPOWERS_RELEASE_COMMIT=${ALLPOWERS_RELEASE_COMMIT}"
    "-DALLPOWERS_PARENT_IDENTITY=${ALLPOWERS_PARENT_IDENTITY}"
    "-DOUTPUT=${directory}" "-DRESOURCE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}")
  execute_process(COMMAND "${CMAKE_COMMAND}" ${arguments}
    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/BuildIdentity.cmake" COMMAND_ERROR_IS_FATAL ANY)
  set(resources)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc.in")
    list(APPEND resources "${directory}/AllpowersMonitor.rc" "${directory}/AllpowersWidget.rc")
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AllpowersUps.inf.in")
    list(APPEND resources "${CMAKE_BINARY_DIR}/package/AllpowersUps.inf")
  endif()
  add_custom_target(product-identity ALL
    COMMAND "${CMAKE_COMMAND}" ${arguments} -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/BuildIdentity.cmake"
    BYPRODUCTS "${directory}/product-identity.cmake" "${directory}/product-version.h"
      "${directory}/product-version.txt" ${resources}
    VERBATIM)
endfunction()
