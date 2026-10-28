param(
    [Parameter(Mandatory)][ValidateSet('Key', 'Restore', 'Seal')][string]$Mode,
    [string]$Root = (Split-Path $PSScriptRoot -Parent),
    [string]$BuildDirectory = 'build/windows'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = [IO.Path]::GetFullPath($Root)
if (![IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $Root $BuildDirectory }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$install = Join-Path $BuildDirectory 'deps/qt/install'
$manifest = Join-Path $install 'allpowers-qt-cache.json'

function Write-OutputValue([string]$Name, [string]$Value) {
    Write-Output "$Name=$Value"
    if ($env:GITHUB_OUTPUT) { Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "$Name=$Value" -Encoding utf8 }
}

# Exact keys only. Include paths because Qt's installed CMake files can retain
# absolute toolchain/build paths; never assume an SDK is freely relocatable.
$identityTimer = [Diagnostics.Stopwatch]::StartNew()
$compiler = Get-Command cl.exe -ErrorAction Stop
$cmakeVersion = & cmake --version
if ($LASTEXITCODE) { throw 'Cannot identify CMake' }
$ninjaVersion = & ninja --version
if ($LASTEXITCODE) { throw 'Cannot identify Ninja' }
foreach ($name in 'VCToolsVersion', 'WindowsSDKVersion', 'VSCMD_ARG_TGT_ARCH') {
    if (![Environment]::GetEnvironmentVariable($name)) { throw "Missing MSVC environment variable: $name" }
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'The Qt cache requires an x64 MSVC environment' }
function Get-ToolVersion([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing toolchain binary: $Path" }
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    if (!$version.FileVersion) { throw "Missing toolchain binary version: $Path" }
    return '{0}.{1}.{2}.{3}' -f $version.FileMajorPart, $version.FileMinorPart, $version.FileBuildPart, $version.FilePrivatePart
}
$linker = Join-Path (Split-Path $compiler.Source -Parent) 'link.exe'
$compilerVersion = Get-ToolVersion $compiler.Source
$linkerVersion = Get-ToolVersion $linker
$dependencies = & cmake -DGROUP=qt -P (Join-Path $Root 'cmake/DependencyFingerprint.cmake')
if ($LASTEXITCODE) { throw 'Cannot identify Qt dependency inputs' }
$inputs = [ordered]@{
    schema = 3
    platform = 'Windows-x64-Release-static-MT-WinRT'
    install = $install.Replace('\', '/').ToLowerInvariant()
    compiler = $compiler.Source.Replace('\', '/').ToLowerInvariant()
    compilerVersion = $compilerVersion
    linker = $linker.Replace('\', '/').ToLowerInvariant()
    linkerVersion = $linkerVersion
    dependencies = "$dependencies"
    toolset = $env:VCToolsVersion
    sdk = $env:WindowsSDKVersion
    sdkRoot = $env:WindowsSdkDir
    ucrt = $env:UCRTVersion
    cmake = $cmakeVersion[0]
    ninja = "$ninjaVersion"
}
# The computed Qt digest above covers its selected pins and recipes. Hashing
# the shared helper itself would also invalidate Qt for firmware-only edits.
foreach ($file in 'QtStatic.cmake', 'WindowsCompiler.cmake', 'SizeOptimization.cmake',
                  'QtCache.ps1', 'QtCacheProbe.cmake', 'QtCacheImports.cmake', 'qt_cache_probe.cpp', 'QtSourceIdentity.cmake') {
    $inputs[$file] = (Get-FileHash -LiteralPath (Join-Path $Root "cmake/$file") -Algorithm SHA256).Hash
}
$fingerprint = $inputs | ConvertTo-Json -Compress
$sha = [Security.Cryptography.SHA256]::Create()
try { $digest = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($fingerprint))).Replace('-', '').ToLowerInvariant() }
finally { $sha.Dispose() }
$key = "qt-static-v3-windows-x64-$digest"
$keySeconds = $identityTimer.Elapsed.TotalSeconds.ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
Write-Host "Qt cache identity: cl $compilerVersion, link $linkerVersion, SDK $env:WindowsSDKVersion; computed in ${keySeconds}s"
if ($Mode -eq 'Key') {
    Write-OutputValue key $key
    Write-OutputValue path $install.Replace('\', '/')
    Write-OutputValue key-seconds $keySeconds
    exit 0
}

function Test-QtSdk {
    & cmake -DMODE=VERIFY "-DPREFIX=$install" "-DSOURCE_CACHE=$Root/build/deps/downloads" `
        -P "$Root/cmake/QtSourceIdentity.cmake"
    if ($LASTEXITCODE) { throw 'Qt SDK/source identity check failed' }
    $probe = Join-Path $BuildDirectory 'qt-cache-probe'
    New-Item -ItemType Directory -Path "$probe/source" -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Root 'cmake/QtCacheProbe.cmake') -Destination "$probe/source/CMakeLists.txt"
    & cmake --fresh -S "$probe/source" -B "$probe/build" -G Ninja `
        '-DCMAKE_BUILD_TYPE=Release' "-DALLPOWERS_ROOT=$Root" "-DCMAKE_PREFIX_PATH=$install" `
        "-DQt6_DIR=$install/lib/cmake/Qt6"
    if ($LASTEXITCODE) { throw 'Qt SDK configure check failed' }
    & cmake --build "$probe/build" --parallel 2
    if ($LASTEXITCODE) { throw 'Qt SDK compile/link check failed' }
    & ctest --test-dir "$probe/build" --output-on-failure
    if ($LASTEXITCODE) { throw 'Qt SDK offline runtime check failed' }
}

if ($Mode -eq 'Restore') {
    if (Test-Path -LiteralPath $install) {
        try {
            if (!(Test-Path -LiteralPath $manifest -PathType Leaf)) { throw 'Missing Qt compatibility manifest' }
            $saved = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
            if ($saved.key -cne $key -or $saved.fingerprint -cne $fingerprint) { throw 'Qt compatibility manifest does not match this build' }
            Test-QtSdk
            Write-OutputValue root $install.Replace('\', '/')
            Write-Host 'Reusing verified static Qt SDK'
            exit 0
        } catch {
            Write-Warning "Qt cache rejected; building from source: $_"
            # Keep diagnostic evidence and leave the managed install path empty.
            # Both paths are fixed descendants of the explicitly selected build directory.
            $rejected = Join-Path $BuildDirectory ('qt-cache-rejected-' + [guid]::NewGuid().ToString('N'))
            Move-Item -LiteralPath $install -Destination $rejected
        }
    }
    Write-OutputValue root ''
    exit 0
}

# Seal only a successfully built AND independently checked SDK. The workflow
# saves this install directory, never ExternalProject stamps or object files.
Test-QtSdk
@{ key = $key; fingerprint = $fingerprint } | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding utf8
Write-Host 'Static Qt SDK verified and ready to cache'
