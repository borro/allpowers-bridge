$ErrorActionPreference='Stop'
$failed=$false
foreach($name in 'Prepare-UpsLab.ps1','Sign-UpsLabPackage.ps1','Test-UpsLab.ps1') {
  $tokens=$null;$parseErrors=$null
  [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name),[ref]$tokens,[ref]$parseErrors) | Out-Null
  if($parseErrors){$failed=$true;$parseErrors | ForEach-Object {Write-Error $_.ToString() -ErrorAction Continue}}
}
if($failed){exit 1}
'Lab scripts parse successfully; no lab operations were executed.'
