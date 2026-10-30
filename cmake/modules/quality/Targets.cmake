include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../common/Arguments.cmake")

function(ap_add_maintenance_targets)
  cmake_parse_arguments(PARSE_ARGV 0 AP "" "ROOT;BUILD_ROOT" "")
  ap_require_arguments(PREFIX AP REQUIRED ROOT BUILD_ROOT)
  set(PROJECT_SOURCE_DIR "${AP_ROOT}")
  set(PROJECT_BINARY_DIR "${AP_BUILD_ROOT}")
  add_custom_target(audit COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/scripts/quality/Audit.cmake" VERBATIM)
  add_custom_target(format-check COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    "-DWORK=${PROJECT_BINARY_DIR}/format" -DCHECK_CONTROLS=ON
    -P "${PROJECT_SOURCE_DIR}/cmake/scripts/quality/RunFormat.cmake" VERBATIM)
  add_custom_target(pre-commit COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    -DPHASE=commit -P "${PROJECT_SOURCE_DIR}/cmake/scripts/hooks/GitHooks.cmake" USES_TERMINAL VERBATIM)
  add_custom_target(pre-push COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    -DPHASE=push -DREVISION=HEAD -P "${PROJECT_SOURCE_DIR}/cmake/scripts/hooks/GitHooks.cmake" USES_TERMINAL VERBATIM)
  add_custom_target(install-git-hooks COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    -P "${PROJECT_SOURCE_DIR}/cmake/scripts/hooks/InstallGitHooks.cmake" USES_TERMINAL VERBATIM)
  add_custom_target(audit-worktree COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    -DMODE=worktree -P "${PROJECT_SOURCE_DIR}/cmake/scripts/quality/Audit.cmake" VERBATIM)
  add_custom_target(audit-history COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    -DMODE=history -P "${PROJECT_SOURCE_DIR}/cmake/scripts/quality/Audit.cmake" VERBATIM)
  add_custom_target(audit-secrets COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
    "-DDEPS_DIR=${ALLPOWERS_DEPS_DIR}" "-DWORK=${PROJECT_BINARY_DIR}/tools/gitleaks/${CMAKE_HOST_SYSTEM_NAME}"
    -P "${PROJECT_SOURCE_DIR}/cmake/scripts/quality/CheckSecrets.cmake" VERBATIM)
endfunction()
