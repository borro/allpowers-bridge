param([Parameter(Mandatory)][string]$Monitor,
      [ValidateSet('PrepareReplay','Probe','RestoreRealBle')][string]$Stage='Probe')
$ErrorActionPreference='Stop'
$machine=Get-CimInstance Win32_ComputerSystem
if(($machine.Manufacturer+' '+$machine.Model) -notmatch 'Virtual Machine|VMware|VirtualBox|QEMU') {
  throw 'This tool is only for the guest VM; host power/device state must not be changed'
}
$monitorPath=(Resolve-Path -LiteralPath $Monitor).Path
$data=Join-Path $env:ProgramData 'AllpowersMonitor'
if($Stage -eq 'Probe') {
  $report=Join-Path $data 'battery-probe.json'
  & $monitorPath --battery-probe 60 --report $report
  if($LASTEXITCODE){throw 'Battery probe failed'}
  Get-Content -LiteralPath $report
  return
}
$service=Get-Service AllpowersMonitor -ErrorAction Stop
Stop-Service AllpowersMonitor
$settings=Join-Path $data 'settings.json'
$settingsBackup=Join-Path $data 'lab-settings-backup.json'
if($Stage -eq 'RestoreRealBle') {
  if(Test-Path -LiteralPath $settingsBackup) {
    $saved=Get-Content -LiteralPath $settingsBackup -Raw | ConvertFrom-Json
    if($saved.existed) {
      [IO.File]::WriteAllBytes($settings,[Convert]::FromBase64String($saved.bytes))
    } elseif(Test-Path -LiteralPath $settings) {
      Remove-Item -LiteralPath $settings
    }
    Remove-Item -LiteralPath $settingsBackup
  }
  & sc.exe config AllpowersMonitor binPath= ('"'+$monitorPath+'" --service')
  if($LASTEXITCODE){throw 'Service configuration failed'}
  Start-Service AllpowersMonitor
  return
}
if(-not (Test-Path -LiteralPath $data)){throw 'Install integration before creating replay data'}
if(-not (Test-Path -LiteralPath $settingsBackup)) {
  $exists=Test-Path -LiteralPath $settings
  $bytes=if($exists){[Convert]::ToBase64String([IO.File]::ReadAllBytes($settings))}else{''}
  @{existed=$exists;bytes=$bytes} | ConvertTo-Json | Set-Content -LiteralPath $settingsBackup -Encoding UTF8
}
# Synthetic history belongs to a separate synthetic station, never the real peer.
@{address='02:00:00:00:00:FF';name='Synthetic UPS Lab';profile=3} |
  ConvertTo-Json | Set-Content -LiteralPath $settings -Encoding UTF8
function Frame([int]$At,[int]$Percent,[int]$Source,[bool]$Runtime,[bool]$Repeat) {
  $known=1+2+4+16+32+64+256+512
  if($Runtime){$known+=8}
  $snapshot=@{abi_version=1;model=3;known=$known;main_seen=1;settings_seen=1;fresh=1;soc=$Percent;
    input_w=0;output_w=140;runtime_s=600;source=$Source;source_raw=$(if($Source -eq 2){0}else{65535});
    ac_output=0;usb_output=0;dc_output=0;main_age_ms=0;settings_age_ms=0;valid_frames=10;invalid_frames=0}
  @{at_ms=$At;snapshot=$snapshot;repeat=$Repeat}
}
# Native power actions remain configured by Windows. First run this scenario
# with critical actions disabled in the GUEST; enable them only after a snapshot.
$scenario=@(
  (Frame 0 100 2 $true $true),
  (Frame 5000 50 1 $true $true),
  (Frame 10000 50 2 $false $true),
  (Frame 15000 50 1 $false $false),
  (Frame 35000 50 2 $true $true),
  (Frame 40000 5 1 $true $true),
  (Frame 50000 0 1 $true $true)
)
$scenario | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $data 'lab-replay.json') -Encoding UTF8
& sc.exe config AllpowersMonitor binPath= ('"'+$monitorPath+'" --service --lab-replay')
if($LASTEXITCODE){throw 'Service replay configuration failed'}
Start-Service AllpowersMonitor
'Replay enabled in guest only; run -Stage Probe to capture native battery state and power events.'
