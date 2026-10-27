# Hash the actual compilation inputs, not the hosted VM's weekly image number.
function Get-ToolchainFingerprint {
    param([string]$Compiler)
    $sdkVersion = $env:WindowsSDKVersion.TrimEnd('\', '/')
    $roots = @(
        (Split-Path $Compiler -Parent),
        (Join-Path $env:VCToolsInstallDir 'include'),
        (Join-Path $env:VCToolsInstallDir 'lib/x64'),
        (Join-Path $env:WindowsSdkDir "Include/$sdkVersion"),
        (Join-Path $env:WindowsSdkDir "Lib/$sdkVersion/um/x64"),
        (Join-Path $env:WindowsSdkDir "Lib/$sdkVersion/ucrt/x64"),
        (Join-Path $env:WindowsSdkDir "bin/$sdkVersion/x64")
    )
    $digest = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
    try {
        foreach ($root in $roots) {
            if (!(Test-Path -LiteralPath $root -PathType Container)) { throw "Missing toolchain directory: $root" }
            foreach ($file in (Get-ChildItem -LiteralPath $root -File -Recurse | Sort-Object FullName)) {
                $relative = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
                $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
                $digest.AppendData([Text.Encoding]::UTF8.GetBytes("$root/$relative=$hash`n"))
            }
        }
        return [Convert]::ToHexString($digest.GetHashAndReset()).ToLowerInvariant()
    } finally { $digest.Dispose() }
}
