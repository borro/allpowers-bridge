// Test-only Win32 substitution; see usb_serial_contract_test.cpp.
#define SetupDiGetClassDevsW Fake::SetupDiGetClassDevsW
#define SetupDiEnumDeviceInfo Fake::SetupDiEnumDeviceInfo
#define CM_Get_Device_IDW Fake::CM_Get_Device_IDW
#define CM_Get_Parent Fake::CM_Get_Parent
#define SetupDiOpenDevRegKey Fake::SetupDiOpenDevRegKey
#define RegQueryValueExW Fake::RegQueryValueExW
#define RegCloseKey Fake::RegCloseKey
#define SetupDiDestroyDeviceInfoList Fake::SetupDiDestroyDeviceInfoList
#define CreateFileW Fake::CreateFileW
#define CloseHandle Fake::CloseHandle
#define GetLastError Fake::GetLastError
#define GetCommState Fake::GetCommState
#define SetCommState Fake::SetCommState
#define SetCommTimeouts Fake::SetCommTimeouts
#define ReadFile Fake::ReadFile
#define WriteFile Fake::WriteFile
