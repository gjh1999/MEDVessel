$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot 'experiments\nnunet_frangi\work\daily-research'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
if (Test-Path (Join-Path $outputDir 'STOP')) { exit 0 }
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmmss'
$prompt = @'
Continue the authorized MEDVessel long-running research project. Read experiments/nnunet_frangi/LONG_RUN_PLAN.md first and inspect actual status files, logs, results and active processes. The user authorizes this daily 07:00 review to reason about progress, optimize the next experiment and execute the necessary work, not merely report. Prioritize audit, robust queue implementation and the 35/70/140-case learning curve. Preserve held-out test cases, source data, existing weights and predictions. Do not duplicate active training; use a single GPU queue. Respect permissions and approval review; record any blocked action rather than bypass controls. Do not publish, push, send messages, buy services, or delete artifacts. Do not spawn agents. Record decisions, actual actions, failures, next steps and resource use in a dated Markdown report under experiments/nnunet_frangi/work/daily-research. Follow the plan's 60 GPU-hour batch ceiling; do not automatically reset it daily. A STOP file in that daily-research directory means no new work. Make concrete progress and leave resumable background jobs when appropriate. Do not claim completion without verified results.
'@
$prompt | & 'C:\Users\gjh\AppData\Roaming\npm\codex.ps1' exec -C $projectRoot --approve-for-me --color never -o (Join-Path $outputDir "$stamp-summary.md") - *> (Join-Path $outputDir "$stamp.log")
exit $LASTEXITCODE
