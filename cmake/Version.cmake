get_filename_component(ALLPOWERS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(Git QUIET)
set(ALLPOWERS_VERSION "")
if(GIT_FOUND AND EXISTS "${ALLPOWERS_ROOT}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*" --dirty
    WORKING_DIRECTORY "${ALLPOWERS_ROOT}" OUTPUT_VARIABLE ALLPOWERS_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
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
message(STATUS "ALLPOWERS version: ${ALLPOWERS_VERSION}")
