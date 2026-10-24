add_executable(models_test models_test.cpp)
target_include_directories(models_test PRIVATE "${PROJECT_SOURCE_DIR}/core/src")
add_executable(abi_test abi_test.cpp)
add_executable(ups_test ups_test.cpp)
target_include_directories(ups_test PRIVATE "${PROJECT_SOURCE_DIR}/core/src" "${PROJECT_SOURCE_DIR}/core/native")
target_link_libraries(abi_test PRIVATE Allpowers::Core)
target_compile_definitions(abi_test PRIVATE
  FIXTURE_PATH="${PROJECT_SOURCE_DIR}/tests/fixtures/notifications.json")
foreach(target models_test abi_test ups_test)
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endforeach()
add_custom_target(native-tests DEPENDS models_test abi_test ups_test)
add_test(NAME models COMMAND models_test)
add_test(NAME abi COMMAND abi_test)
add_test(NAME ups-state COMMAND ups_test)
if(WIN32)
  # Process-local policy only: parse the lab scripts without executing any of
  # their operations or changing the machine's saved execution policy.
  add_test(NAME lab-script-syntax COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
    -File "${PROJECT_SOURCE_DIR}/tools/Check-LabScripts.ps1")
  set_tests_properties(abi PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:allpowers_core>")
endif()
