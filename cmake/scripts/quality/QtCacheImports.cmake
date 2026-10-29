cmake_minimum_required(VERSION 4.4)
execute_process(COMMAND "${DUMPBIN}" /nologo /dependents "${EXE}"
  OUTPUT_VARIABLE imports COMMAND_ERROR_IS_FATAL ANY)
string(TOLOWER "${imports}" imports)
if(imports MATCHES "(qt[0-9]|msvcp[0-9]|vcruntime|concrt|ucrtbase)[a-z0-9_]*\\.dll")
  message(FATAL_ERROR "Qt SDK probe imports a non-static Qt/C++ runtime: ${imports}")
endif()
