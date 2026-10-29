cmake_minimum_required(VERSION 4.4)
file(MAKE_DIRECTORY "${WORK}")
# Extraction is confined to a disposable catalog. Normal builds never modify
# the checked-in translation source.
foreach(language ru zh_CN zh_TW)
  set(catalog "${WORK}/allpowers_${language}.ts")
  file(COPY_FILE "${SOURCE}/translations/allpowers_${language}.ts" "${catalog}")
  execute_process(COMMAND "${LUPDATE}" "${SOURCE}" -no-obsolete -locations none
    -ts "${catalog}" COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${LRELEASE}" -fail-on-unfinished
    "${catalog}" -qm "${WORK}/allpowers_${language}.qm" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
