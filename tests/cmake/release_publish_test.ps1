param(
  [Parameter(Mandatory)][string]$Root,
  [Parameter(Mandatory)][string]$Work,
  [Parameter(Mandatory)][string]$Assets,
  [Parameter(Mandatory)][string]$Tag
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$Work = (Resolve-Path -LiteralPath $Work).Path
$Root = (Resolve-Path -LiteralPath $Root).Path
$Assets = (Resolve-Path -LiteralPath $Assets).Path
$repo = Join-Path $Work 'repo'
New-Item -ItemType Directory -Force -Path $repo | Out-Null
Push-Location $repo
$previousState = $env:ALLPOWERS_RELEASE_TEST_STATE
try {
  & git init -q
  & git config user.name 'Release Test'
  & git config user.email 'release@example.invalid'
  & git config commit.gpgsign false
  'Synthetic publisher test' | Set-Content README.md
  & git add README.md
  & git commit -qm 'Synthetic release fixture'
  if ($LASTEXITCODE) { throw 'Cannot create isolated publication fixture' }
  $commit = (& git rev-parse HEAD).Trim()
  $fake = Join-Path $Work 'fake-gh.ps1'
  @'
param([Parameter(ValueFromRemainingArguments)][string[]]$Arguments)
$ErrorActionPreference = 'Stop'
$path = $env:ALLPOWERS_RELEASE_TEST_STATE
$state = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
$state.calls = @($state.calls) + ($Arguments -join ' ')
function Save-State { $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding utf8 }
Save-State
if ($Arguments[0] -eq 'api' -and $Arguments[1] -like '*/commits/*') {
  if ($state.mode -eq 'moved-tag' -and $state.uploaded) { '0000000000000000000000000000000000000000' }
  else { $state.commit }
  exit 0
}
if ($Arguments[0] -eq 'api') {
  if ($state.mode -eq 'lookup-forbidden') { Write-Error 'Forbidden (HTTP 403)' -ErrorAction Continue; exit 1 }
  if ($Arguments[1] -like '*/releases/tags/*' -and $state.draft) {
    Write-Error 'Drafts are unavailable by tag (HTTP 404)' -ErrorAction Continue; exit 1
  }
  $listing = $Arguments[1] -like '*/releases?per_page=100'
  if (-not $state.exists) {
    if ($listing) { '[[]]'; exit 0 }
    Write-Error 'Not Found (HTTP 404)' -ErrorAction Continue; exit 1
  }
  $remote = @()
  if ($state.uploaded) {
    foreach ($file in Get-ChildItem -LiteralPath $state.assets -File) {
      $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
      if ($state.mode -eq 'bad-digest' -or $state.mode -eq 'published-mismatch') { $hash = 'bad' }
      $remote += @{ name=$file.Name; state='uploaded'; size=$file.Length; digest=('sha256:'+$hash) }
    }
  }
  if ($state.mode -eq 'partial-upload' -and $remote.Count) { $remote = @($remote | Select-Object -Skip 1) }
  if ($state.mode -eq 'foreign-draft') { $remote += @{name='foreign.bin';state='uploaded';size=1;digest='unknown'} }
  $release = @{ id=42; tag_name=$state.tag; draft=$state.draft; target_commitish='main'; body=$state.body; assets=$remote }
  if ($listing) {
    if ($Arguments -notcontains '--paginate' -or $Arguments -notcontains '--slurp') { throw 'Draft lookup must include all pages' }
    $page = @($release)
    if ($state.mode -eq 'duplicate-tag') { $page += $release }
    $pages = @(,$page)
    if ($state.mode -eq 'resume-draft') { $pages = @(@(@{id=41;tag_name='v0.0.1';draft=$false}),$page) }
    ConvertTo-Json -InputObject $pages -Depth 8 -Compress
  } else {
    if ($Arguments[1] -notlike '*/releases/42') { throw 'Verification must address the draft by ID' }
    $release | ConvertTo-Json -Depth 5 -Compress
  }
  exit 0
}
switch ($Arguments[1]) {
  'create' {
    $state.exists=$true; $state.draft=$true
    $notesIndex = [Array]::IndexOf($Arguments, '--notes-file')
    $state.body = Get-Content -LiteralPath $Arguments[$notesIndex + 1] -Raw
  }
  'upload' {
    if ($state.mode -eq 'upload-failure') { Write-Error 'Synthetic upload interruption' -ErrorAction Continue; exit 1 }
    $state.uploaded=$true
  }
  'edit' { $state.draft=$false; $state.published=$true }
  default { throw 'Unexpected fake CLI invocation' }
}
Save-State
exit 0
'@ | Set-Content -LiteralPath $fake -Encoding utf8
  foreach ($mode in @('success','resume-draft','upload-failure','partial-upload','bad-digest',
      'moved-tag','lookup-forbidden','foreign-draft','old-draft','duplicate-tag','published-match','published-mismatch')) {
    $env:ALLPOWERS_RELEASE_TEST_STATE = Join-Path $Work ($mode + '.json')
    $published = $mode.StartsWith('published-')
    @{
      mode=$mode; commit=$commit; assets=$Assets; calls=@(); published=$false; tag=$Tag;
      exists=($published -or $mode -in @('resume-draft','foreign-draft','old-draft','duplicate-tag'));
      uploaded=$published; draft=(-not $published)
      body=('<!-- allpowers-release-commit: ' + $(if($mode -eq 'old-draft') {'0000000000000000000000000000000000000000'} else {$commit}) + ' -->')
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $env:ALLPOWERS_RELEASE_TEST_STATE -Encoding utf8
    & pwsh -NoProfile -File (Join-Path $Root 'cmake/PublishRelease.ps1') -Repository 'example/project' `
      -Tag $Tag -Assets $Assets -GitHubCli $fake *> (Join-Path $Work ($mode + '.log'))
    $code = $LASTEXITCODE
    $state = Get-Content -LiteralPath $env:ALLPOWERS_RELEASE_TEST_STATE -Raw | ConvertFrom-Json
    $pass = $mode -in @('success','resume-draft','published-match')
    if (($pass -and $code -ne 0) -or (-not $pass -and $code -eq 0)) {
      throw "Wrong publisher result for $mode (exit $code); inspect the isolated test log"
    }
    $mutations = @($state.calls | Where-Object { $_ -match '^release (create|upload|edit) ' })
    if ($mode -eq 'published-match' -or $mode -eq 'published-mismatch' -or
        $mode -in @('lookup-forbidden','foreign-draft','old-draft','duplicate-tag')) {
      if ($mutations.Count) { throw "Existing/unknown release was changed: $mode" }
    }
    if ($mode -in @('success','resume-draft')) {
      $prerelease = ($Tag -split '\+', 2)[0].Contains('-')
      $flags = '--draft=false --prerelease=' + $prerelease.ToString().ToLowerInvariant() +
        ' --latest=' + (-not $prerelease).ToString().ToLowerInvariant()
      if (-not $state.published -or -not ($state.calls | Where-Object { $_.Contains($flags) })) {
        throw 'Verified release was not published with the correct flags'
      }
    } elseif ($state.published) { throw "Incomplete/mismatched release was published: $mode" }
  }
  Write-Output 'Draft resume, interrupted upload, remote tag, digest and published-release guards passed (mock API only)'
} finally {
  $env:ALLPOWERS_RELEASE_TEST_STATE = $previousState
  Pop-Location
}
