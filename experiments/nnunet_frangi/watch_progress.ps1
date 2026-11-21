$Host.UI.RawUI.WindowTitle = 'MEDVessel training progress'
$taskWork = Join-Path $PSScriptRoot 'work\full-real-100'
while ($true) {
    $taskState = $null
    try { $taskState = Get-Content -LiteralPath (Join-Path $taskWork 'pipeline_status.json') -Raw | ConvertFrom-Json } catch {}
    $taskLog = Join-Path $taskWork 'prepare.log'
    $taskPrepared = @(Select-String -LiteralPath $taskLog -Pattern '^Prepared ' -ErrorAction SilentlyContinue).Count
    $taskPct = [Math]::Min(100, $taskPrepared)
    Clear-Host
    Write-Host 'MEDVessel | CTA + Frangi | 100 cases' -ForegroundColor Cyan
    Write-Host ('Updated: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
    Write-Host ('Pipeline stage: ' + $taskState.stage)
    Write-Host ''
    $taskBars = [int][Math]::Floor($taskPct / 2)
    Write-Host ('Preparation [' + ('#' * $taskBars) + ('-' * (50 - $taskBars)) + '] ' + $taskPrepared + '/100')
    if ($taskState.stage -eq 'prepare' -and $taskPrepared -gt 0) {
        try {
            $taskStart = [DateTimeOffset]::Parse($taskState.updated_utc)
            $taskElapsed = ([DateTimeOffset]::UtcNow - $taskStart).TotalSeconds
            $taskRemaining = [TimeSpan]::FromSeconds($taskElapsed / $taskPrepared * (100 - $taskPrepared))
            Write-Host ('Estimated preparation remaining: {0:0.0} hours (rough estimate)' -f $taskRemaining.TotalHours)
        } catch {}
    }
    Write-Host ''
    Write-Host 'Latest preparation output:'
    Get-Content -LiteralPath $taskLog -Tail 3 -ErrorAction SilentlyContinue
    $taskTrain = Join-Path $taskWork 'training_status.json'
    if (Test-Path -LiteralPath $taskTrain) {
        Write-Host ''; Write-Host 'Training status:' -ForegroundColor Yellow
        Get-Content -LiteralPath $taskTrain -ErrorAction SilentlyContinue
        Get-Content -LiteralPath (Join-Path $taskWork 'training.log') -Tail 8 -ErrorAction SilentlyContinue
    }
    Write-Host ''; Write-Host 'Evaluation status:'
    Get-Content -LiteralPath (Join-Path $taskWork 'evaluation_status.json') -ErrorAction SilentlyContinue
    Write-Host ''; Write-Host 'Refreshes every 5 seconds. Closing this window does not stop training.' -ForegroundColor DarkGray
    Start-Sleep -Seconds 5
}
