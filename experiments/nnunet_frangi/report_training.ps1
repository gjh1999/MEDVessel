param([switch]$Once, [switch]$NoNotification)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$taskWork = Join-Path $PSScriptRoot 'work\full-real-100'
$taskMutex = New-Object System.Threading.Mutex($false, 'Local\MEDVesselTrainingReporter')
if (-not $taskMutex.WaitOne(0)) { exit }
$taskIcon = New-Object System.Windows.Forms.NotifyIcon
$taskIcon.Icon = [System.Drawing.SystemIcons]::Information
$taskIcon.Text = 'MEDVessel training'
$taskIcon.Visible = -not $NoNotification
function Read-TaskJson($taskPath) {
    try { Get-Content -LiteralPath $taskPath -Raw | ConvertFrom-Json } catch { $null }
}
try {
    while ($true) {
        if (Test-Path -LiteralPath (Join-Path $taskWork 'STOP_REPORTING')) { break }
        $taskGuardian = Read-TaskJson (Join-Path $taskWork 'guardian_status.json')
        $taskLogPath = Join-Path $taskWork 'training.log'
        $taskLines = @(Get-Content -LiteralPath $taskLogPath -Tail 3000)
        $taskEpochs = @($taskLines | Select-String 'Epoch (\d+)\s*$')
        $taskEpoch = if ($taskEpochs.Count) { $taskEpochs[-1].Matches[0].Groups[1].Value } else { 'unknown' }
        $taskBest = @($taskLines | Select-String 'New best EMA pseudo Dice: ([\d.]+)')
        $taskDice = if ($taskBest.Count) { $taskBest[-1].Matches[0].Groups[1].Value } else { 'unknown' }
        $taskTimes = @($taskLines | Select-String 'Epoch time: ([\d.]+) s' | Select-Object -Last 10)
        $taskAverage = if ($taskTimes.Count) { [Math]::Round(($taskTimes | ForEach-Object { [double]$_.Matches[0].Groups[1].Value } | Measure-Object -Average).Average, 1) } else { 0 }
        $taskEarlyFile = Get-ChildItem -LiteralPath (Join-Path $taskWork 'nnUNet_results') -Recurse -Filter 'early_stopping.json' | Select-Object -First 1
        $taskEarly = if ($taskEarlyFile) { Read-TaskJson $taskEarlyFile.FullName } else { $null }
        $taskState = if ($taskGuardian) { $taskGuardian.stage } else { 'unknown' }
        if ($taskState -eq 'running' -and $taskGuardian) {
            if (-not (Get-Process -Id $taskGuardian.pid -ErrorAction SilentlyContinue)) { $taskState = 'guardian_missing' }
        }
        $taskAge = [Math]::Round(((Get-Date) - (Get-Item -LiteralPath $taskLogPath).LastWriteTime).TotalMinutes, 1)
        $taskReport = [ordered]@{
            time = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')
            stage = $taskState
            epoch = $taskEpoch
            best_ema_dice_in_recent_log = $taskDice
            seconds_per_epoch = $taskAverage
            stale_epochs = $taskEarly.epochs_without_significant_improvement
            patience = $taskEarly.patience
            attempt = $taskGuardian.attempt
            log_age_minutes = $taskAge
        }
        $taskJson = $taskReport | ConvertTo-Json -Compress
        Add-Content -LiteralPath (Join-Path $taskWork 'training_reports.jsonl') -Value $taskJson -Encoding UTF8
        $taskMessage = "$(Get-Date -Format 'MM-dd HH:mm') | $taskState | Epoch $taskEpoch`r`nEMA Dice: $taskDice | ${taskAverage}s/epoch`r`nEarly stop: $($taskReport.stale_epochs)/$($taskReport.patience) | Attempt: $($taskReport.attempt) | Log age: ${taskAge}m"
        Set-Content -LiteralPath (Join-Path $taskWork 'training_report_latest.txt') -Value $taskMessage -Encoding UTF8
        if (-not $NoNotification) {
            $taskIcon.ShowBalloonTip(15000, 'MEDVessel training report', $taskMessage, [System.Windows.Forms.ToolTipIcon]::Info)
            $taskUntil = (Get-Date).AddSeconds(16)
            while ((Get-Date) -lt $taskUntil) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 100 }
        }
        if ($Once -or $taskState -in @('completed', 'stopped')) { break }
        $taskNext = (Get-Date).AddSeconds($(if ($NoNotification) { 900 } else { 884 }))
        while ((Get-Date) -lt $taskNext) {
            if (Test-Path -LiteralPath (Join-Path $taskWork 'STOP_REPORTING')) { return }
            [System.Windows.Forms.Application]::DoEvents()
            Start-Sleep -Seconds 1
        }
    }
} finally {
    $taskIcon.Dispose()
    $taskMutex.ReleaseMutex()
    $taskMutex.Dispose()
}
