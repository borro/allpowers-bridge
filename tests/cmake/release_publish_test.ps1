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
  if ($state.mode -eq 'missing-tag') { Write-Error 'Unknown tag (HTTP 404)' -ErrorAction Continue; exit 1 }
  if ($state.mode -eq 'moved-tag' -and $state.uploaded) { '0000000000000000000000000000000000000000' }
  else { $state.commit }
  exit 0
}
function Get-Release {
  $remote = @()
  $index = 0
  foreach ($file in Get-ChildItem -LiteralPath $state.assets -File | Sort-Object Name) {
    $index++
    if ($file.Name -cin $state.uploadedNames) {
      $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
      if ($state.mode -in @('bad-digest','published-mismatch') -or
          ($state.mode -eq 'resume-draft' -and $index -eq 2 -and -not $state.replaced)) { $hash = 'bad' }
      $remote += @{ id=(100+$index); name=$file.Name; state='uploaded'; size=$file.Length; digest=('sha256:'+$hash) }
    }
  }
  if ($state.mode -eq 'partial-upload' -and $remote.Count) { $remote = @($remote | Select-Object -Skip 1) }
  if ($state.mode -eq 'foreign-draft') { $remote += @{name='foreign.bin';state='uploaded';size=1;digest='unknown'} }
  return @{ id=42; tag_name=$state.tag; draft=$state.draft; target_commitish=$state.commit;
    prerelease=($state.tag -split '\+',2)[0].Contains('-'); body=$state.body; assets=$remote }
}
if ($Arguments[0] -ne 'api') { throw 'Do not rediscover a created draft through gh release commands' }
$endpoint = $Arguments[1]
$methodIndex = [Array]::IndexOf($Arguments, '--method')
$method = if ($methodIndex -ge 0) { $Arguments[$methodIndex+1] } else { 'GET' }
if ($endpoint -like '*/releases?per_page=100' -and $method -eq 'GET') {
  if ($state.mode -eq 'lookup-forbidden') { Write-Error 'Forbidden (HTTP 403)' -ErrorAction Continue; exit 1 }
  if ($Arguments -notcontains '--paginate' -or $Arguments -notcontains '--slurp') { throw 'Draft lookup must include all pages' }
  if (-not $state.exists -or $state.mode -eq 'stale-list-after-create') { '[[]]'; exit 0 }
  $page = @((Get-Release))
  if ($state.mode -eq 'duplicate-tag') { $page += Get-Release }
  $pages = @(,$page)
  if ($state.mode -eq 'resume-draft') { $pages = @(@(@{id=41;tag_name='v0.0.1';draft=$false}),$page) }
  ConvertTo-Json -InputObject $pages -Depth 8 -Compress
  exit 0
}
if ($endpoint -eq 'repos/example/project/releases' -and $method -eq 'POST') {
  if ($state.exists) { throw 'A retry must resume the existing draft' }
  $inputIndex = [Array]::IndexOf($Arguments, '--input')
  $request = Get-Content -LiteralPath $Arguments[$inputIndex+1] -Raw | ConvertFrom-Json
  if ($request.PSObject.Properties.Name -contains 'target_commitish') {
    Write-Error 'Old workflow target requires unavailable Workflows permission (HTTP 403)' -ErrorAction Continue
    exit 1
  }
  if ($request.tag_name -cne $state.tag -or
      $request.draft -ne $true -or $request.prerelease -ne ($state.tag -split '\+',2)[0].Contains('-') -or
      -not $request.body.Contains("<!-- allpowers-release-commit: $($state.commit) -->") -or
      -not $request.body.Contains('- Synthetic version-specific change.') -or
      -not $request.body.Contains('FIRMWARE-SOURCES.md') -or
      -not $request.body.Contains("https://github.com/example/project/blob/$($state.tag)/cmake/Dependencies.cmake")) {
    throw 'Invalid draft creation payload'
  }
  $state.exists=$true; $state.draft=$true; $state.body=$request.body
  Save-State
  $release = Get-Release
  switch ($state.mode) {
    'created-wrong-tag' { $release.tag_name='v9.9.9' }
    'created-wrong-commit' { $release.body='Unrelated draft' }
    'created-no-id' { $release.id=$null }
    'created-not-draft' { $release.draft=$false }
  }
  $release | ConvertTo-Json -Depth 8 -Compress
  exit 0
}
if (-not $state.exists) { throw 'Cannot operate on an unknown draft' }
if ($endpoint -eq 'repos/example/project/releases/assets/102' -and $method -eq 'DELETE') {
  if ($state.mode -ne 'resume-draft') { throw 'Unexpected asset deletion' }
  $state.replaced=$true
  $second = (Get-ChildItem -LiteralPath $state.assets -File | Sort-Object Name)[1].Name
  $state.uploadedNames=@($state.uploadedNames | Where-Object { $_ -cne $second })
  Save-State
  exit 0
}
if ($endpoint.StartsWith('https://uploads.github.com/repos/example/project/releases/42/assets?name=') -and $method -eq 'POST') {
  if ($state.mode -eq 'upload-failure') { Write-Error 'Synthetic upload interruption' -ErrorAction Continue; exit 1 }
  $inputIndex = [Array]::IndexOf($Arguments, '--input')
  $file = Get-Item -LiteralPath $Arguments[$inputIndex+1]
  $name = [Uri]::UnescapeDataString(($endpoint -split '\?name=',2)[1])
  if ($name -cne $file.Name -or $Arguments -notcontains 'Content-Type: application/octet-stream') {
    throw 'Incorrect binary upload request'
  }
  if ($name -cin $state.uploadedNames) { throw 'Unchanged assets must not be uploaded again' }
  $state.uploaded=$true
  $state.uploadedNames=@($state.uploadedNames)+$name
  Save-State
  '{}'
  exit 0
}
if ($endpoint -eq 'repos/example/project/releases/42') {
  if ($method -eq 'PATCH') {
    $prerelease = ($state.tag -split '\+',2)[0].Contains('-')
    foreach ($field in @('draft=false',('prerelease='+$prerelease.ToString().ToLowerInvariant()),
        ('make_latest='+(-not $prerelease).ToString().ToLowerInvariant()))) {
      if ($Arguments -notcontains $field) { throw 'Incorrect publication flags' }
    }
    $bodyField = @($Arguments | Where-Object { $_.StartsWith('body=') })
    if ($bodyField.Count -ne 1 -or -not $bodyField[0].Contains('- Synthetic version-specific change.')) {
      throw 'Publication must include version-specific notes, including on draft resume'
    }
    $state.body=$bodyField[0].Substring(5)
    $state.draft=$false; $state.published=$true
    Save-State
  } elseif ($method -ne 'GET') { throw 'Unexpected release method' }
  $release = Get-Release
  if ($state.mode -eq 'changed-id') { $release.id=43 }
  $release | ConvertTo-Json -Depth 8 -Compress
  exit 0
}
throw 'Unexpected fake API invocation'
'@ | Set-Content -LiteralPath $fake -Encoding utf8
  foreach ($mode in @('stale-list-after-create','success','resume-draft','upload-failure','partial-upload','bad-digest',
      'created-wrong-tag','created-wrong-commit','created-no-id','created-not-draft','changed-id',
      'moved-tag','missing-tag','lookup-forbidden','foreign-draft','old-draft','duplicate-tag','published-match','published-mismatch',
      'missing-notes','empty-notes','wrong-version-notes','no-changes-notes',
      'duplicate-checksum','missing-checksum','checksum-mismatch','redundant-runtime','redundant-sidecar')) {
    $env:ALLPOWERS_RELEASE_TEST_STATE = Join-Path $Work ($mode + '.json')
    $published = $mode.StartsWith('published-')
    $modeAssets = $Assets
    if ($mode -in @('duplicate-checksum','missing-checksum','checksum-mismatch','redundant-runtime','redundant-sidecar')) {
      $modeAssets = Join-Path $Work ($mode + '-assets')
      New-Item -ItemType Directory -Force -Path $modeAssets | Out-Null
      Get-ChildItem -LiteralPath $Assets -File | Copy-Item -Destination $modeAssets
      $sums = Join-Path $modeAssets 'SHA256SUMS'
      $records = @(Get-Content -LiteralPath $sums)
      switch ($mode) {
        'duplicate-checksum' { Add-Content -LiteralPath $sums -Value $records[0] }
        'missing-checksum' { $records | Select-Object -Skip 1 | Set-Content -LiteralPath $sums }
        'checksum-mismatch' { $records[0] = ('0' * 64) + $records[0].Substring(64); $records | Set-Content -LiteralPath $sums }
        'redundant-runtime' { 'Duplicate EXEs' | Set-Content -LiteralPath (Join-Path $modeAssets 'AllpowersMonitor-redundant-win-x64.zip') }
        'redundant-sidecar' { $records[0] | Set-Content -LiteralPath (Join-Path $modeAssets 'duplicate.sha256') }
      }
    }
    @{
      mode=$mode; commit=$commit; assets=$modeAssets; calls=@(); published=$false; tag=$Tag;
      exists=($published -or $mode -in @('resume-draft','foreign-draft','old-draft','duplicate-tag'));
      uploaded=$published; draft=(-not $published); replaced=$false
      uploadedNames=@(if ($published) { (Get-ChildItem -LiteralPath $Assets -File).Name }
        elseif ($mode -eq 'resume-draft') { (Get-ChildItem -LiteralPath $Assets -File | Sort-Object Name | Select-Object -First 2).Name })
      body=('<!-- allpowers-release-commit: ' + $(if($mode -eq 'old-draft') {'0000000000000000000000000000000000000000'} else {$commit}) + ' -->')
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $env:ALLPOWERS_RELEASE_TEST_STATE -Encoding utf8
    $notes = Join-Path $Work ($mode + '-notes.md')
    $content = "# ALLPOWERS Bridge $Tag`n`n- Synthetic version-specific change.`n"
    switch ($mode) {
      'empty-notes' { $content = '' }
      'wrong-version-notes' { $content = "# ALLPOWERS Bridge v9.9.9`n`n- Unrelated changes.`n" }
      'no-changes-notes' { $content = "# ALLPOWERS Bridge $Tag`n`n" }
    }
    if ($mode -ne 'missing-notes') { $content | Set-Content -LiteralPath $notes -Encoding utf8 }
    & pwsh -NoProfile -File (Join-Path $Root 'cmake/PublishRelease.ps1') -Repository 'example/project' `
      -Tag $Tag -Assets $modeAssets -NotesFile $notes -GitHubCli $fake *> (Join-Path $Work ($mode + '.log'))
    $code = $LASTEXITCODE
    $state = Get-Content -LiteralPath $env:ALLPOWERS_RELEASE_TEST_STATE -Raw | ConvertFrom-Json
    $pass = $mode -in @('stale-list-after-create','success','resume-draft','published-match')
    if (($pass -and $code -ne 0) -or (-not $pass -and $code -eq 0)) {
      throw "Wrong publisher result for $mode (exit $code); inspect the isolated test log"
    }
    $mutations = @($state.calls | Where-Object { $_ -match '--method (POST|DELETE|PATCH)' })
    if ($mode -eq 'published-match' -or $mode -eq 'published-mismatch' -or
        $mode -in @('missing-tag','lookup-forbidden','foreign-draft','old-draft','duplicate-tag',
          'missing-notes','empty-notes','wrong-version-notes','no-changes-notes',
          'duplicate-checksum','missing-checksum','checksum-mismatch','redundant-runtime','redundant-sidecar')) {
      if ($mutations.Count) { throw "Existing/unknown release was changed: $mode" }
    }
    if ($mode -in @('stale-list-after-create','success','resume-draft')) {
      if (-not $state.published -or @($state.uploadedNames).Count -ne 6) {
        throw 'Verified release was not completely published'
      }
      $lists = @($state.calls | Where-Object { $_ -like 'api */releases?per_page=100 *' })
      if ($lists.Count -ne 1) { throw 'New drafts must not depend on release-list visibility' }
      if ($mode -eq 'resume-draft' -and -not $state.replaced) { throw 'Interrupted upload was not repaired' }
    } elseif ($mode -like 'created-*') {
      if ($mutations.Count -ne 1) { throw 'An invalid creation response must stop before upload' }
    } elseif ($state.published) { throw "Incomplete/mismatched release was published: $mode" }
  }
  Write-Output 'Version-specific notes, missing/mismatched note guards, draft resume and publication guards passed (mock API only)'
} finally {
  $env:ALLPOWERS_RELEASE_TEST_STATE = $previousState
  Pop-Location
}
