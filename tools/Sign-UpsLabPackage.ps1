param([Parameter(Mandatory)][string]$Package,
      [Parameter(Mandatory)][string]$SignTool,
      [Parameter(Mandatory)][string]$Inf2Cat)
$ErrorActionPreference='Stop'
$machine=Get-CimInstance Win32_ComputerSystem
if(($machine.Manufacturer+' '+$machine.Model) -notmatch 'Virtual Machine|VMware|VirtualBox|QEMU') {
  throw 'Test certificates may only be created inside the dedicated guest VM'
}
if(-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Guest Administrator privileges are required'
}
$packageRoot=(Resolve-Path -LiteralPath $Package).Path
foreach($name in 'AllpowersUps.sys','AllpowersUps.inf') {
  if(-not (Test-Path -LiteralPath (Join-Path $packageRoot $name))){throw "Missing $name"}
}
foreach($tool in $SignTool,$Inf2Cat) {
  $signature=Get-AuthenticodeSignature -LiteralPath $tool
  if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft'){throw 'Only original Microsoft SDK signing tools are accepted'}
}
# No bcdedit, Secure Boot change, or reboot is performed by this script.
$certificate=Get-ChildItem Cert:/LocalMachine/My | Where-Object {
  $_.Subject -eq 'CN=ALLPOWERS UPS Lab' -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(1)
} | Select-Object -First 1
if(-not $certificate) {
  $certificate=New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=ALLPOWERS UPS Lab' -CertStoreLocation Cert:/LocalMachine/My -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddMonths(6)
}
$public=Join-Path $packageRoot 'AllpowersUps-Lab.cer'
Export-Certificate -Cert $certificate -FilePath $public | Out-Null
Import-Certificate -FilePath $public -CertStoreLocation Cert:/LocalMachine/Root | Out-Null
Import-Certificate -FilePath $public -CertStoreLocation Cert:/LocalMachine/TrustedPublisher | Out-Null
& $SignTool sign /sm /s My /sha1 $certificate.Thumbprint /fd SHA256 (Join-Path $packageRoot 'AllpowersUps.sys')
if($LASTEXITCODE){throw 'SYS signing failed'}
& $Inf2Cat "/driver:$packageRoot" /os:10_X64 /uselocaltime
if($LASTEXITCODE){throw 'Catalog generation failed'}
& $SignTool sign /sm /s My /sha1 $certificate.Thumbprint /fd SHA256 (Join-Path $packageRoot 'AllpowersUps.cat')
if($LASTEXITCODE){throw 'Catalog signing failed'}
& $SignTool verify /pa /v (Join-Path $packageRoot 'AllpowersUps.sys')
if($LASTEXITCODE){throw 'SYS verification failed'}
& $SignTool verify /pa /v (Join-Path $packageRoot 'AllpowersUps.cat')
if($LASTEXITCODE){throw 'Catalog verification failed'}
'Signed payload ready. Rebuild the single EXE with ALLPOWERS_DRIVER_PACKAGE_DIR pointing to these three signed payload files.'
