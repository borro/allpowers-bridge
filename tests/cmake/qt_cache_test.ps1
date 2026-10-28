param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Work)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# A new directory per run keeps these rejection tests independent of real SDKs.
$scratch = Join-Path $Work ([guid]::NewGuid().ToString('N'))
$source = Join-Path $scratch 'source'
New-Item -ItemType Directory -Path "$source/cmake" -Force | Out-Null
foreach ($file in 'Dependencies.cmake', 'QtStatic.cmake', 'WindowsCompiler.cmake', 'SizeOptimization.cmake',
                  'DependencyFingerprint.cmake',
                  'QtCache.ps1', 'QtCacheProbe.cmake', 'QtCacheImports.cmake', 'qt_cache_probe.cpp', 'QtSourceIdentity.cmake') {
    Copy-Item -LiteralPath "$Root/cmake/$file" -Destination "$source/cmake/$file"
}
$savedOutput = $env:GITHUB_OUTPUT
$savedVersions = @{}
foreach ($name in 'UCRTVersion', 'WindowsSDKVersion', 'VCToolsVersion') {
    $savedVersions[$name] = [Environment]::GetEnvironmentVariable($name)
}
$powershell = (Get-Process -Id $PID).Path
try {
    $env:GITHUB_OUTPUT = Join-Path $scratch 'outputs.txt'
    $script = Join-Path $source 'cmake/QtCache.ps1'
    function Invoke-Cache([string]$Mode) {
        Set-Content -LiteralPath $env:GITHUB_OUTPUT -Value ''
        & $powershell -NoProfile -File $script -Mode $Mode -Root $source | Out-Host
        if ($LASTEXITCODE) { throw "Qt cache $Mode failed" }
        return Get-Content -LiteralPath $env:GITHUB_OUTPUT
    }
    function Get-Key {
        $output = Invoke-Cache Key
        return ($output | Where-Object { $_ -like 'key=*' })
    }
    $baseline = Get-Key
    if (!$baseline -or (Get-Key) -cne $baseline) { throw 'Identical inputs did not produce a stable key' }
    foreach ($name in $savedVersions.Keys) {
        try {
            [Environment]::SetEnvironmentVariable($name, '0.0.0.0')
            if ((Get-Key) -ceq $baseline) { throw "$name did not invalidate the key" }
        } finally { [Environment]::SetEnvironmentVariable($name, $savedVersions[$name]) }
    }
    $dependencies = Get-Content -LiteralPath "$source/cmake/Dependencies.cmake" -Raw
    # Exercise the complete PowerShell key, not just CMake's component digest.
    # Firmware-only edits to the shared helper must not invalidate the Qt SDK.
    $helperPath = "$source/cmake/DependencyFingerprint.cmake"
    $helper = Get-Content -LiteralPath $helperPath -Raw
    $changedHelper = $helper.Replace('set(recipes PrepareFirmwareKit.cmake ', 'set(recipes FirmwareOnlyFixture.cmake PrepareFirmwareKit.cmake ')
    if ($changedHelper -ceq $helper) { throw 'Firmware helper fixture did not change' }
    try {
        Set-Content -LiteralPath $helperPath -Value $changedHelper
        if ((Get-Key) -cne $baseline) { throw 'Firmware helper recipe invalidated unrelated Qt SDK' }
    } finally { Set-Content -LiteralPath $helperPath -Value $helper }
    foreach ($name in 'ALLPOWERS_MULL_VERSION', 'ALLPOWERS_ESP32_VERSION',
                      'ALLPOWERS_QT_VERSION', 'ALLPOWERS_QTBASE_SHA256', 'ALLPOWERS_CXX_STANDARD') {
        $pattern = '(?m)^(set\(' + [regex]::Escape($name) + '\s+)"[^"]+"(\))'
        $changed = [regex]::Replace($dependencies, $pattern, '${1}"cache-test-change"${2}')
        if ($changed -ceq $dependencies) { throw "$name fixture did not change" }
        try {
            Set-Content -LiteralPath "$source/cmake/Dependencies.cmake" -Value $changed
            $same = (Get-Key) -ceq $baseline
            if ($name -in 'ALLPOWERS_MULL_VERSION', 'ALLPOWERS_ESP32_VERSION') {
                if (!$same) { throw "$name invalidated unrelated Qt SDK" }
            } elseif ($same) { throw "$name did not invalidate the Qt SDK" }
        } finally { Set-Content -LiteralPath "$source/cmake/Dependencies.cmake" -Value $dependencies }
    }
    Add-Content -LiteralPath "$source/cmake/QtStatic.cmake" -Value '# changed build recipe'
    if ((Get-Key) -ceq $baseline) { throw 'Qt build recipe did not invalidate the key' }
    $recipeKey = Get-Key
    Add-Content -LiteralPath "$source/cmake/SizeOptimization.cmake" -Value '# changed size policy'
    if ((Get-Key) -ceq $recipeKey) { throw 'Size policy did not invalidate the key' }
    $output = Invoke-Cache Restore
    if ('root=' -notin $output) { throw 'An absent SDK must request a source build' }
    $install = Join-Path $source 'build/windows/deps/qt/install'
    New-Item -ItemType Directory -Path $install -Force | Out-Null
    Set-Content -LiteralPath "$install/sentinel" -Value 'incomplete SDK'
    $output = Invoke-Cache Restore
    if ('root=' -notin $output -or (Test-Path -LiteralPath $install)) { throw 'Incomplete SDK was not rejected' }
    New-Item -ItemType Directory -Path $install -Force | Out-Null
    @{ key = 'incompatible'; fingerprint = 'incompatible' } | ConvertTo-Json |
        Set-Content -LiteralPath "$install/allpowers-qt-cache.json"
    $output = Invoke-Cache Restore
    if ('root=' -notin $output -or (Test-Path -LiteralPath $install)) { throw 'Incompatible SDK was not rejected' }
    $rejected = @(Get-ChildItem -LiteralPath "$source/build/windows" -Directory -Filter 'qt-cache-rejected-*')
    if ($rejected.Count -ne 2) { throw 'Rejected SDKs were not preserved for diagnostics' }
    Write-Host 'Qt cache key, invalidation, miss, and rejection checks passed'
} finally {
    $env:GITHUB_OUTPUT = $savedOutput
    foreach ($name in $savedVersions.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedVersions[$name])
    }
}
