param(
  [Parameter(Mandatory)][string]$Path,
  [Parameter(Mandatory)][string]$ExpectedVersion
)
$ErrorActionPreference = 'Stop'
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path -LiteralPath $Path).Path)
if ($info.ProductVersion -cne $ExpectedVersion -or $info.FileVersion -cne $ExpectedVersion) {
  throw "EXE version mismatch: expected $ExpectedVersion, got ProductVersion='$($info.ProductVersion)', FileVersion='$($info.FileVersion)'"
}
$numeric = $ExpectedVersion -split '[-+]', 2
$parts = $numeric[0].Split('.')
if ($info.FileMajorPart -ne [int]$parts[0] -or $info.FileMinorPart -ne [int]$parts[1] -or
    $info.FileBuildPart -ne [int]$parts[2] -or $info.FilePrivatePart -ne 0) {
  throw 'EXE numeric VERSIONINFO does not match the release version'
}
Write-Output "Verified EXE version: $ExpectedVersion"
