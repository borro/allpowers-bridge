# Restore and independently verify either a cache or this run's SDK artifact.
# A different hosted toolchain rebuilds locally; consumers never save shared caches.
param([string]$Root = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
& cmake "-DSOURCE_CACHE=$Root/build/deps/downloads" -P "$Root/cmake/PrepareQtSources.cmake"
if ($LASTEXITCODE) { throw 'Qt source preparation failed' }
& cmake "-DDEPS_DIR=$Root/build/deps" -P "$Root/cmake/SetupWdk.cmake"
if ($LASTEXITCODE) { throw 'WDK preparation failed' }
$lines = & "$Root/cmake/QtCache.ps1" -Mode Restore -Root $Root
if ($LASTEXITCODE) { throw 'Qt SDK validation failed' }
$lines | Write-Host
$rootLine = @($lines | Where-Object { $_ -is [string] -and $_.StartsWith('root=') })
if ($rootLine.Count -ne 1) { throw 'Missing Qt SDK restore result' }
$qtRoot = $rootLine[0].Substring(5)
if (!$qtRoot) {
    Write-Host 'Building static Qt from source with 4 compiler jobs. Qt progress is sampled every 30 seconds; [n/total] is the outer build graph.'
    & cmake --preset windows -S $Root '-DALLPOWERS_QT_ROOT=' -DALLPOWERS_QT_JOBS=4 -DALLPOWERS_BUILD_JOBS=4
    if ($LASTEXITCODE) { throw 'Qt bootstrap configure failed' }
    . "$Root/cmake/QtBuildProgress.ps1"
    $progress = Start-QtBuildProgress -BuildDirectory "$Root/build/windows"
    try {
        & cmake --build "$Root/build/windows" --target qt-static --parallel 2
        $qtBuildExit = $LASTEXITCODE
    } finally {
        if ($progress.State -eq 'Failed') { Write-Warning 'Qt progress observer stopped; full logs remain in the build directory.' }
        Stop-Job -Job $progress -ErrorAction SilentlyContinue
        Remove-Job -Job $progress -Force -ErrorAction SilentlyContinue
    }
    if ($qtBuildExit) { throw "Qt bootstrap build failed (exit $qtBuildExit)" }
    Write-Host 'Qt build and installation completed; verifying source identity, linking and offline runtime before caching.'
    & "$Root/cmake/QtCache.ps1" -Mode Seal -Root $Root
}
