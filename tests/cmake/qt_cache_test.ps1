param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Work)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# A new directory per run keeps these rejection tests independent of real SDKs.
$scratch = Join-Path $Work ([guid]::NewGuid().ToString('N'))
$source = Join-Path $scratch 'source'
New-Item -ItemType Directory -Path "$source/cmake" -Force | Out-Null
# Exercise content invalidation on a tiny independent toolchain, never real SDK files.
. "$Root/cmake/ToolchainFingerprint.ps1"
$savedToolchain = @{}
foreach ($name in 'VCToolsInstallDir', 'WindowsSdkDir', 'WindowsSDKVersion') {
    $savedToolchain[$name] = [Environment]::GetEnvironmentVariable($name)
}
try {
    $env:VCToolsInstallDir = "$scratch/toolchain/vc"
    $env:WindowsSdkDir = "$scratch/toolchain/sdk"
    $env:WindowsSDKVersion = '1.0'
    foreach ($directory in 'vc/bin', 'vc/include', 'vc/lib/x64', 'sdk/Include/1.0',
                           'sdk/Lib/1.0/um/x64', 'sdk/Lib/1.0/ucrt/x64', 'sdk/bin/1.0/x64') {
        New-Item -ItemType Directory -Path "$scratch/toolchain/$directory" -Force | Out-Null
    }
    $compiler = "$scratch/toolchain/vc/bin/cl.exe"
    Set-Content -LiteralPath $compiler -Value 'synthetic compiler, never executed'
    $before = Get-ToolchainFingerprint $compiler
    Set-Content -LiteralPath "$scratch/toolchain/sdk/Include/1.0/test.h" -Value 'changed header'
    $header = Get-ToolchainFingerprint $compiler
    Set-Content -LiteralPath "$scratch/toolchain/vc/lib/x64/test.lib" -Value 'changed library'
    if ($before -eq $header -or $header -eq (Get-ToolchainFingerprint $compiler)) {
        throw 'Toolchain headers/libraries did not invalidate the fingerprint'
    }
} finally {
    foreach ($name in $savedToolchain.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedToolchain[$name])
    }
}
foreach ($file in 'Dependencies.cmake', 'QtStatic.cmake', 'WindowsCompiler.cmake', 'SizeOptimization.cmake',
                  'DependencyFingerprint.cmake', 'ToolchainFingerprint.ps1',
                  'QtCache.ps1', 'QtCacheProbe.cmake', 'QtCacheImports.cmake', 'qt_cache_probe.cpp', 'QtSourceIdentity.cmake') {
    Copy-Item -LiteralPath "$Root/cmake/$file" -Destination "$source/cmake/$file"
}
$savedOutput = $env:GITHUB_OUTPUT
$savedSdk = $env:UCRTVersion
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
    $env:UCRTVersion = '0.0.0.0'
    if ((Get-Key) -ceq $baseline) { throw 'SDK version did not invalidate the key' }
    $env:UCRTVersion = $savedSdk
    $dependencies = Get-Content -LiteralPath "$source/cmake/Dependencies.cmake" -Raw
    Set-Content -LiteralPath "$source/cmake/Dependencies.cmake" -Value ($dependencies.Replace('"0.35.0"', '"0.99.0"'))
    if ((Get-Key) -cne $baseline) { throw 'Mull version invalidated unrelated Qt SDK' }
    Set-Content -LiteralPath "$source/cmake/Dependencies.cmake" -Value $dependencies
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
    $env:UCRTVersion = $savedSdk
}
