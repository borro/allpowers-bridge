// Test-only Win32 substitution; see usb_serial_contract_test.cpp.
#undef SetupDiGetClassDevsW
#undef SetupDiEnumDeviceInfo
#undef CM_Get_Device_IDW
#undef CM_Get_Parent
#undef SetupDiOpenDevRegKey
#undef RegQueryValueExW
#undef RegCloseKey
#undef SetupDiDestroyDeviceInfoList
#undef CreateFileW
#undef CloseHandle
#undef GetLastError
#undef GetCommState
#undef SetCommState
#undef SetCommTimeouts
#undef ReadFile
#undef WriteFile
