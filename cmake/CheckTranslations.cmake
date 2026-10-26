cmake_minimum_required(VERSION 4.4)
file(MAKE_DIRECTORY "${WORK}")
file(COPY_FILE "${SOURCE}/translations/allpowers_ru.ts" "${WORK}/coverage.ts")
# Extraction is confined to a disposable catalog. Normal builds never modify
# the checked-in translation source.
execute_process(COMMAND "${LUPDATE}" "${SOURCE}" -no-obsolete -locations none
  -ts "${WORK}/coverage.ts" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${LRELEASE}" -fail-on-unfinished
  "${WORK}/coverage.ts" -qm "${WORK}/coverage.qm" COMMAND_ERROR_IS_FATAL ANY)
