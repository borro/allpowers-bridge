param(
  [Parameter(Mandatory)][string]$Repository,
  [Parameter(Mandatory)][string]$Tag,
  [Parameter(Mandatory)][string]$Assets,
  [string]$GitHubCli = 'gh'
)
$ErrorActionPreference = 'Stop'
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or
    $Tag -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$') {
  throw 'Invalid repository or release tag'
}
$Assets = (Resolve-Path -LiteralPath $Assets).Path
$files = @(Get-ChildItem -LiteralPath $Assets -File | Sort-Object Name)
$assetVersion = $Tag.Substring(1).Replace('+', '_')
$payloads = @("AllpowersMonitor-$assetVersion-win-x64.zip", "AllpowersMonitor-$assetVersion-sources.zip",
  "AllpowersMonitor-$assetVersion-win-x64.exe", "AllpowersWidget-$assetVersion-win-x64.exe",
  "AllpowersFirmware-$assetVersion-waveshare-147.zip", "AllpowersFirmware-$assetVersion-pocket-096.zip")
$names = @()
foreach ($line in Get-Content -LiteralPath (Join-Path $Assets 'SHA256SUMS')) {
  if ($line -notmatch '^([a-f0-9]{64})  ([A-Za-z0-9._+-]+)$') { throw 'Invalid SHA256SUMS' }
  $hash = $Matches[1]; $name = $Matches[2]
  if ($name -cnotin $payloads) { throw 'Release asset version/name does not match the tag' }
  $actual = (Get-FileHash -LiteralPath (Join-Path $Assets $name) -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actual -cne $hash -or (Get-Content -LiteralPath (Join-Path $Assets ($name + '.sha256')) -Raw).Trim() -cne $line) {
    throw "Local release checksum mismatch: $name"
  }
  $names += $name, ($name + '.sha256')
}
$names += 'SHA256SUMS'
if ($names.Count -ne 13 -or (Compare-Object -CaseSensitive ($names | Sort-Object) $files.Name)) {
  throw 'Unexpected or incomplete release assets'
}
function Invoke-GitHub([string[]]$Arguments) {
  $result = & $GitHubCli @Arguments
  if ($LASTEXITCODE -ne 0) { throw 'GitHub CLI operation failed; the release was not published' }
  return $result
}
$commit = (& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot read release commit' }
$marker = "<!-- allpowers-release-commit: $commit -->"
$encoded = [Uri]::EscapeDataString($Tag)
function Confirm-RemoteTag {
  $remote = (Invoke-GitHub @('api', "repos/$Repository/commits/$encoded", '--jq', '.sha') | Out-String).Trim()
  if ($remote -cne $commit) { throw 'Remote tag moved or does not identify the built commit' }
}
function Read-Release {
  $json = Invoke-GitHub @('api', "repos/$Repository/releases/tags/$encoded")
  return ($json -join "`n" | ConvertFrom-Json)
}
function Assert-UploadedAssets($Release) {
  if (@($Release.assets).Count -ne $files.Count) { throw 'Uploaded release asset count mismatch' }
  foreach ($file in $files) {
    $remote = @($Release.assets | Where-Object { $_.name -ceq $file.Name })
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($remote.Count -ne 1 -or $remote[0].state -ne 'uploaded' -or
        $remote[0].size -ne $file.Length -or $remote[0].digest -cne ('sha256:' + $hash)) {
      throw "Uploaded release asset failed verification: $($file.Name)"
    }
  }
}
Confirm-RemoteTag
$errorFile = Join-Path $Assets '.stage/release-api-error.txt'
$json = & $GitHubCli api "repos/$Repository/releases/tags/$encoded" 2> $errorFile
if ($LASTEXITCODE -eq 0) {
  $release = $json -join "`n" | ConvertFrom-Json
  if (-not $release.draft) {
    Assert-UploadedAssets $release
    Write-Output "Release already published with matching assets: $Tag"
    return
  }
  if (-not ([string]$release.body).Contains($marker)) { throw 'Existing draft was not prepared for this commit' }
  if (@($release.assets | Where-Object { $_.name -cnotin $files.Name }).Count) {
    throw 'Existing draft contains unexpected assets; refusing to modify it'
  }
} else {
  if ((Get-Content -LiteralPath $errorFile -Raw) -notmatch 'HTTP 404') {
    throw 'Cannot determine whether the release exists'
  }
  $notes = Join-Path $Assets '.stage/release-notes.md'
  @"
Built and tested from **$Tag** on Windows and Ubuntu.
$marker

- Windows Monitor and desktop Widget: standalone EXEs and a combined ZIP.
- Separate Waveshare 1.47 and Pocket 0.96 firmware packages, with matching board manifests.
- Complete application and Qt corresponding sources, with SHA256 checksums.

The applications work without the optional Windows UPS driver. That integration remains
experimental, unsigned and unverified in a VM. Firmware updates require the matching
board profile; settings and private recovery images are not part of these assets.
"@ | Set-Content -LiteralPath $notes -Encoding utf8
  $arguments = @('release', 'create', $Tag, '--repo', $Repository, '--verify-tag', '--target', $commit,
    '--title', "ALLPOWERS Bridge $Tag", '--draft', '--notes-file', $notes)
  if (($Tag -split '\+', 2)[0].Contains('-')) { $arguments += '--prerelease' }
  Invoke-GitHub $arguments
}
# Only an unpublished draft may be resumed/replaced after an interrupted upload.
Invoke-GitHub (@('release', 'upload', $Tag, '--repo', $Repository, '--clobber') + $files.FullName)
Assert-UploadedAssets (Read-Release)
Confirm-RemoteTag
$prerelease = ($Tag -split '\+', 2)[0].Contains('-')
$arguments = @('release', 'edit', $Tag, '--repo', $Repository, '--draft=false',
  ('--prerelease=' + $prerelease.ToString().ToLowerInvariant()),
  ('--latest=' + (-not $prerelease).ToString().ToLowerInvariant()))
Invoke-GitHub $arguments
Write-Output "Published verified release: $Tag"
