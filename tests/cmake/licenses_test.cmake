cmake_minimum_required(VERSION 4.4)
file(TO_CMAKE_PATH "${ROOT}" ROOT)
file(TO_CMAKE_PATH "${WORK}" WORK)
include("${ROOT}/cmake/CollectQtNotices.cmake")
file(MAKE_DIRECTORY "${WORK}/source/component" "${WORK}/source/LICENSES")
file(WRITE "${WORK}/source/component/LICENCE.md" "PCRE license\n")
file(WRITE "${WORK}/source/component/BDF-LICENSE.txt" "BDF attribution\n")
file(WRITE "${WORK}/source/component/terms.custom" "Referenced license\n")
file(WRITE "${WORK}/source/component/authors.custom" "Referenced copyright\n")
file(WRITE "${WORK}/source/LICENSES/LGPL-3.0-only.txt" "LGPL\n")
file(WRITE "${WORK}/source/component/qt_attribution.json" [=[
[{"LicenseFile":"LICENCE.md","Path":"unused"},
 {"LicenseFiles":["BDF-LICENSE.txt","terms.custom"],"CopyrightFile":"authors.custom"}]
]=])
ap_collect_qt_notices("${WORK}/source" "${WORK}/notices")
foreach(name LICENCE.md BDF-LICENSE.txt terms.custom authors.custom qt_attribution.json)
  file(SHA256 "${WORK}/source/component/${name}" source_hash)
  file(SHA256 "${WORK}/notices/component/${name}" notice_hash)
  if(NOT source_hash STREQUAL notice_hash)
    message(FATAL_ERROR "Lost license/attribution: ${name}")
  endif()
endforeach()
file(WRITE "${WORK}/source/component/qt_attribution.json" "{\"LicenseFile\":\"missing.txt\"}")
file(WRITE "${WORK}/missing.cmake"
  "include(\"${ROOT}/cmake/CollectQtNotices.cmake\")\nap_collect_qt_notices(\"${WORK}/source\" \"${WORK}/notices\")\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${WORK}/missing.cmake" RESULT_VARIABLE result
  OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "Missing/unsafe Qt attribution")
  message(FATAL_ERROR "A missing upstream license must stop packaging")
endif()

# Tiny independent source/SDK fixture: test the rejection paths without a Qt
# build, and without changing the real dependency cache or installed SDK.
set(identity "${WORK}/identity")
file(MAKE_DIRECTORY "${identity}/scripts" "${identity}/archives" "${identity}/sdk/lib")
file(COPY_FILE "${ROOT}/cmake/QtSourceIdentity.cmake" "${identity}/scripts/QtSourceIdentity.cmake")
set(dependencies "set(ALLPOWERS_QT_VERSION test)\n")
foreach(module qtbase qtconnectivity qttools)
  set(source "${identity}/sources/${module}-everywhere-src-test")
  file(MAKE_DIRECTORY "${source}")
  file(WRITE "${source}/original.cpp" "Original ${module}\n")
  set(archive "${identity}/archives/${module}-everywhere-src-test.tar.xz")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cJf "${archive}" -- "${module}-everywhere-src-test"
    WORKING_DIRECTORY "${identity}/sources" COMMAND_ERROR_IS_FATAL ANY)
  file(SHA256 "${archive}" hash)
  string(TOUPPER "${module}" key)
  string(APPEND dependencies "set(ALLPOWERS_${key}_SHA256 ${hash})\n")
endforeach()
file(WRITE "${identity}/scripts/Dependencies.cmake" "${dependencies}")
file(MAKE_DIRECTORY "${identity}/sdk/bin")
file(WRITE "${identity}/sdk/bin/lrelease.exe" "Original lrelease\n")
file(WRITE "${identity}/sdk/bin/lupdate.exe" "Original lupdate\n")
file(WRITE "${identity}/sdk/lib/Qt6Core.lib" "Original static library\n")
function(check_identity mode expected)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DMODE=${mode}" "-DPREFIX=${identity}/sdk"
    "-DSOURCE_CACHE=${identity}/archives" "-DWORK=${identity}/reference"
    "-Dqtbase_SOURCE=${identity}/sources/qtbase-everywhere-src-test"
    "-Dqttools_SOURCE=${identity}/sources/qttools-everywhere-src-test"
    "-Dqtconnectivity_SOURCE=${identity}/sources/qtconnectivity-everywhere-src-test"
    -P "${identity}/scripts/QtSourceIdentity.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(expected STREQUAL pass AND NOT result EQUAL 0)
    message(FATAL_ERROR "Valid source identity rejected: ${output}${error}")
  elseif(expected STREQUAL fail AND result EQUAL 0)
    message(FATAL_ERROR "Changed/unverified SDK was accepted by ${mode}")
  endif()
endfunction()
check_identity(SEAL pass)
check_identity(VERIFY pass)
file(APPEND "${identity}/sdk/lib/Qt6Core.lib" "Changed library\n")
check_identity(VERIFY fail)
file(MAKE_DIRECTORY "${identity}/sdk/bin")
file(WRITE "${identity}/sdk/bin/lrelease.exe" "Original lrelease\n")
file(WRITE "${identity}/sdk/bin/lupdate.exe" "Original lupdate\n")
file(WRITE "${identity}/sdk/lib/Qt6Core.lib" "Original static library\n")
file(APPEND "${identity}/sources/qtbase-everywhere-src-test/original.cpp" "Changed source\n")
check_identity(SEAL fail)
file(REMOVE "${identity}/sdk/allpowers-source-identity.cmake")
check_identity(VERIFY fail)
