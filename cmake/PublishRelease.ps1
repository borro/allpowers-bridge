param(
  [Parameter(Mandatory)][string]$Repository,
  [Parameter(Mandatory)][string]$Tag,
  [Parameter(Mandatory)][string]$Assets,
  [string]$NotesFile,
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
function Find-Release {
  # The tag endpoint only returns published releases. Drafts must be found in
  # the authenticated, paginated release list, including on an upload retry.
  $json = Invoke-GitHub @('api', "repos/$Repository/releases?per_page=100", '--paginate', '--slurp')
  $pages = $json -join "`n" | ConvertFrom-Json
  $matches = @(foreach ($page in $pages) {
    foreach ($item in $page) { if ($item.tag_name -ceq $Tag) { $item } }
  })
  if ($matches.Count -gt 1) { throw 'Multiple releases identify this tag' }
  if ($matches.Count -eq 1) { return $matches[0] }
}
function Read-Release([long]$ReleaseId) {
  $json = Invoke-GitHub @('api', "repos/$Repository/releases/$ReleaseId")
  return ($json -join "`n" | ConvertFrom-Json)
}
function Assert-Draft($Release) {
  if ($Release.id -notmatch '^[1-9][0-9]*$' -or $Release.tag_name -cne $Tag -or
      -not $Release.draft -or -not ([string]$Release.body).Contains($marker)) {
    throw 'Draft identity does not match this tag and commit'
  }
  if (@($Release.assets | Where-Object { $_.name -cnotin $files.Name }).Count) {
    throw 'Existing draft contains unexpected assets; refusing to modify it'
  }
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
$release = Find-Release
if ($release) {
  if (-not $release.draft) {
    Assert-UploadedAssets $release
    Write-Output "Release already published with matching assets: $Tag"
    return
  }
  Assert-Draft $release
}
if (-not $NotesFile) { $NotesFile = Join-Path $PSScriptRoot "../docs/releases/$Tag.md" }
$changes = (Get-Content -LiteralPath $NotesFile -Raw -Encoding utf8).Trim()
if (-not $changes.StartsWith("# ALLPOWERS Bridge $Tag`n") -and
    -not $changes.StartsWith("# ALLPOWERS Bridge $Tag`r`n")) {
  throw 'Release notes must start with a heading matching the exact tag'
}
if ($changes -notmatch '(?m)^- \S') { throw 'Release notes must describe the changes in this version' }
$body = @"
$changes

## Downloads

Built and tested from **$Tag** on Windows and Ubuntu.
$marker

- Windows Monitor and desktop Widget: standalone EXEs and a combined ZIP.
- Separate Waveshare 1.47 and Pocket 0.96 firmware packages, with matching board manifests.
- Each firmware package includes license notices, corresponding sources and a tested relinking kit.
- Complete application and Qt corresponding sources, with SHA256 checksums.

The applications work without the optional Windows UPS driver. That integration remains
experimental, unsigned and unverified in a VM. Firmware updates require the matching
board profile; settings and private recovery images are not part of these assets.
"@
if (-not $release) {
  $notes = Join-Path $Assets '.stage/release-notes.md'
  $body | Set-Content -LiteralPath $notes -Encoding utf8
  $request = Join-Path $Assets '.stage/create-release.json'
  @{
    tag_name = $Tag; target_commitish = $commit; name = "ALLPOWERS Bridge $Tag"
    draft = $true; prerelease = ($Tag -split '\+', 2)[0].Contains('-')
    body = Get-Content -LiteralPath $notes -Raw
  } | ConvertTo-Json | Set-Content -LiteralPath $request -Encoding utf8
  # The release list can lag behind creation. Keep the ID returned by POST;
  # never rediscover a newly created draft by tag (including inside gh release).
  $json = Invoke-GitHub @('api', "repos/$Repository/releases", '--method', 'POST', '--input', $request)
  $release = $json -join "`n" | ConvertFrom-Json
}
Assert-Draft $release
$releaseId = [long]$release.id
# Only an unpublished draft may be resumed/replaced after an interrupted upload.
foreach ($file in $files) {
  $existing = @($release.assets | Where-Object { $_.name -ceq $file.Name })
  if ($existing.Count -gt 1) { throw 'Duplicate draft asset names' }
  if ($existing.Count) {
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($existing[0].state -eq 'uploaded' -and $existing[0].size -eq $file.Length -and
        $existing[0].digest -ceq ('sha256:' + $hash)) { continue }
    if ($existing[0].id -notmatch '^[1-9][0-9]*$') { throw 'Invalid draft asset ID' }
    Invoke-GitHub @('api', "repos/$Repository/releases/assets/$($existing[0].id)", '--method', 'DELETE') | Out-Null
  }
  $name = [Uri]::EscapeDataString($file.Name)
  Invoke-GitHub @('api', "https://uploads.github.com/repos/$Repository/releases/$releaseId/assets?name=$name",
    '--method', 'POST', '--header', 'Content-Type: application/octet-stream', '--input', $file.FullName) | Out-Null
}
$uploaded = Read-Release $releaseId
Assert-Draft $uploaded
if ($uploaded.id -ne $releaseId) { throw 'Draft ID changed during upload' }
Assert-UploadedAssets $uploaded
Confirm-RemoteTag
$prerelease = ($Tag -split '\+', 2)[0].Contains('-')
$json = Invoke-GitHub @('api', "repos/$Repository/releases/$releaseId", '--method', 'PATCH',
  '-f', ('body=' + $body),
  '-F', 'draft=false', '-F', ('prerelease=' + $prerelease.ToString().ToLowerInvariant()),
  '-f', ('make_latest=' + (-not $prerelease).ToString().ToLowerInvariant()))
$published = $json -join "`n" | ConvertFrom-Json
if ($published.id -ne $releaseId -or $published.tag_name -cne $Tag -or $published.draft -or
    $published.prerelease -ne $prerelease -or $published.body -cne $body) { throw 'Release publication was not confirmed' }
Assert-UploadedAssets $published
Write-Output "Published verified release: $Tag"
