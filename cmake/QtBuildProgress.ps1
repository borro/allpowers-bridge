# Observe ExternalProject logs without changing Qt's build commands or cache key.
function Start-QtBuildProgress {
    param(
        [Parameter(Mandatory)][string]$BuildDirectory,
        [ValidateRange(1, 300)][int]$IntervalSeconds = 30
    )
    Start-ThreadJob -StreamingHost $Host -ArgumentList $BuildDirectory, $IntervalSeconds -ScriptBlock {
        param($directory, $interval)
        $started = [DateTime]::UtcNow
        $previous = ''
        while ($true) {
            $elapsed = [DateTime]::UtcNow - $started
            $prefix = '[Qt +{0:hh\:mm\:ss}]' -f $elapsed
            # Fixed stamp directories only: never walk the large Qt source tree.
            $logs = foreach ($component in 'qtbase', 'qtconnectivity', 'qttools') {
                Get-ChildItem -Path "$directory/deps/$component/src/$component-static-stamp/*-*.log" `
                    -File -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTimeUtc -ge $started }
            }
            $latest = $logs | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
            if ($latest) {
                $age = [int]([DateTime]::UtcNow - $latest.LastWriteTimeUtc).TotalSeconds
                $identity = '{0}:{1}:{2}' -f $latest.FullName, $latest.Length, $latest.LastWriteTimeUtc.Ticks
                $phase = $latest.BaseName -replace '-static', '' -replace '-(out|err)$', ''
                Write-Host "$prefix latest log: $phase; updated ${age}s ago ($($latest.Length) bytes)"
                if ($identity -ne $previous) {
                    $lines = @(Get-Content -LiteralPath $latest.FullName -Tail 10 -ErrorAction SilentlyContinue |
                        Where-Object { $_.Trim() } | Select-Object -Last 3)
                    foreach ($line in $lines) {
                        $short = $line.Substring(0, [Math]::Min(240, $line.Length))
                        Write-Host "$prefix   $short"
                    }
                    $previous = $identity
                }
            } else {
                Write-Host "$prefix waiting for new Qt build log output"
            }
            Start-Sleep -Seconds $interval
        }
    }
}
