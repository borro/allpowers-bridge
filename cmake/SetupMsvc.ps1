param([string]$DependenciesFile = (Join-Path $PSScriptRoot 'Dependencies.cmake'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$pins = Get-Content -LiteralPath $DependenciesFile -Raw
function Read-Version([string]$Name) {
    $match = [regex]::Match($pins, '(?m)^set\(' + [regex]::Escape($Name) + ' "([0-9]+(?:\.[0-9]+)+)"\)')
    if (!$match.Success) { throw "Missing dependency version: $Name" }
    return $match.Groups[1].Value
}
$toolset = Read-Version 'ALLPOWERS_MSVC_TOOLSET_VERSION'
$sdk = Read-Version 'ALLPOWERS_WINDOWS_SDK_VERSION'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$installation) { throw 'Cannot locate Visual Studio C++ Build Tools' }
$vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
if (!(Test-Path -LiteralPath $vcvars -PathType Leaf)) { throw 'vcvarsall.bat is missing' }

# Capture the environment in memory; never print inherited credentials or tokens.
# Pin both inputs and verify the result: vcvarsall can report some errors with
# a successful exit code, so its exit status alone is not sufficient.
$output = & $env:ComSpec /d /s /c "`"$vcvars`" x64 $sdk -vcvars_ver=$toolset >nul && set"
if ($LASTEXITCODE -ne 0) { throw "Cannot initialize MSVC $toolset / Windows SDK $sdk" }
$configured = @{}
foreach ($line in $output) {
    if ($line -match '^([^=]+)=(.*)$') { $configured[$matches[1]] = $matches[2] }
}
if (!$configured['VCToolsVersion'] -or !$configured['VCToolsVersion'].StartsWith("$toolset.")) {
    throw "Expected MSVC $toolset.x, got '$($configured['VCToolsVersion'])'"
}
if (!$configured['WindowsSDKVersion'] -or $configured['WindowsSDKVersion'].TrimEnd('\') -ne $sdk) {
    throw "Expected Windows SDK $sdk, got '$($configured['WindowsSDKVersion'])'"
}
if ($configured['VSCMD_ARG_TGT_ARCH'] -ne 'x64' -or $configured['VSCMD_ARG_HOST_ARCH'] -ne 'x64') {
    throw 'Expected an x64 host and target compiler environment'
}

foreach ($name in $configured.Keys) {
    # Export only vcvarsall's changes; GitHub-owned variables must not be rewritten.
    if ($name -match '^(GITHUB_|RUNNER_|NODE_OPTIONS$)' -or
        $configured[$name] -ceq [Environment]::GetEnvironmentVariable($name)) { continue }
    $value = $configured[$name]
    if ($name -in @('PATH', 'INCLUDE', 'LIB', 'LIBPATH')) {
        $value = ($value.Split(';') | Select-Object -Unique) -join ';'
    }
    [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    if ($env:GITHUB_ENV) {
        Add-Content -LiteralPath $env:GITHUB_ENV -Value "$name=$value" -Encoding utf8
    }
}
$expectedCompiler = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'
if ([IO.Path]::GetFullPath((Get-Command cl.exe).Source) -ine [IO.Path]::GetFullPath($expectedCompiler)) {
    throw 'PATH does not select the configured MSVC compiler'
}
Write-Host "Configured MSVC $env:VCToolsVersion / Windows SDK $sdk (x64)"
