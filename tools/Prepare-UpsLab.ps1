param(
  [ValidateSet('Inspect','EnableHyperV','Create','Checkpoint')][string]$Stage='Inspect',
  [string]$Iso
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$labRoot=[IO.Path]::GetFullPath((Join-Path $projectRoot '.local/ups-lab'))
if(-not $labRoot.StartsWith($projectRoot+[IO.Path]::DirectorySeparatorChar)){throw 'Invalid lab directory'}
New-Item -ItemType Directory -Path $labRoot -Force | Out-Null
$vmName='Allpowers-UPS-Lab'
$admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if($Stage -eq 'Inspect') {
  [PSCustomObject]@{Administrator=$admin; HyperVModule=[bool](Get-Module -ListAvailable Hyper-V); LabDirectory=$labRoot}
  return
}
if(-not $admin){throw 'Run this lab preparation script as Administrator. No boot/restart action was taken.'}
if($Stage -eq 'EnableHyperV') {
  $result=Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All -All -NoRestart
  [PSCustomObject]@{Stage=$Stage;RestartNeeded=$result.RestartNeeded;Completed=$true} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $labRoot 'hyperv-status.json') -Encoding UTF8
  return
}
Import-Module Hyper-V
$existing=Get-VM -Name $vmName -ErrorAction SilentlyContinue
if($existing -and -not ([IO.Path]::GetFullPath($existing.Path)).StartsWith($labRoot+[IO.Path]::DirectorySeparatorChar)) {
  throw 'A VM with this name exists outside the dedicated lab directory; refusing to change it'
}
if($Stage -eq 'Checkpoint') {
  if(-not $existing){throw 'Lab VM does not exist'}
  Checkpoint-VM -Name $vmName -SnapshotName ('Before-ALLPOWERS-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
  return
}
if($existing){throw 'Lab VM already exists; refusing to replace its disks'}
if(-not $Iso){throw 'Specify the official Windows 11 x64 ISO with -Iso'}
$isoPath=(Resolve-Path -LiteralPath $Iso).Path
$switch=Get-VMSwitch -Name 'Default Switch' -ErrorAction SilentlyContinue
if(-not $switch){throw 'Hyper-V Default Switch is unavailable. Configure a lab switch before creating the VM.'}
$vmRoot=Join-Path $labRoot 'vm'
New-Item -ItemType Directory -Path $vmRoot -Force | Out-Null
$disk=Join-Path $vmRoot 'Windows11.vhdx'
if(Test-Path -LiteralPath $disk){throw 'Refusing to reuse an existing virtual disk'}
New-VM -Name $vmName -Generation 2 -MemoryStartupBytes 8GB -NewVHDPath $disk -NewVHDSizeBytes 80GB -Path $vmRoot -SwitchName $switch.Name | Out-Null
Set-VMProcessor -VMName $vmName -Count 4
Set-VM -VMName $vmName -AutomaticStartAction Nothing -AutomaticStopAction ShutDown -CheckpointType Standard
Set-VMKeyProtector -VMName $vmName -NewLocalKeyProtector
Enable-VMTPM -VMName $vmName
# Only the named guest firmware changes; host Secure Boot is untouched.
Set-VMFirmware -VMName $vmName -EnableSecureBoot Off
Add-VMDvdDrive -VMName $vmName -Path $isoPath
$dvd=Get-VMDvdDrive -VMName $vmName
Set-VMFirmware -VMName $vmName -FirstBootDevice $dvd
Enable-VMIntegrationService -VMName $vmName -Name 'Guest Service Interface'
Start-VM -Name $vmName
[PSCustomObject]@{VM=$vmName;Disk=$disk;ISO=$isoPath;Next='Install guest Windows, then create a checkpoint. Never run guest tests on the host.'}
