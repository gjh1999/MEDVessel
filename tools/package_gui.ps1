param(
    [string]$BuildDirectory = 'build/cmake',
    [string]$QtPrefix = 'C:/msys64/ucrt64'
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $repositoryRoot $BuildDirectory }
$destination = Join-Path $repositoryRoot 'build/desktop'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$savedPath = $env:PATH
try {
    $env:PATH = (Join-Path $QtPrefix 'bin') + ';' + $env:PATH
    foreach ($name in @('medvessel_gui.exe','medvessel_cli.exe','medvessel_phantom.exe')) {
        Copy-Item -LiteralPath (Join-Path $BuildDirectory $name) -Destination $destination -Force
    }
    & (Join-Path $QtPrefix 'bin/windeployqt6.exe') --release --no-translations (Join-Path $destination 'medvessel_gui.exe')
    if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }
    # Keep a headless platform for automated capture/smoke checks of this bundle.
    $offscreen = Join-Path $QtPrefix 'share/qt6/plugins/platforms/qoffscreen.dll'
    if (Test-Path -LiteralPath $offscreen) {
        Copy-Item -LiteralPath $offscreen -Destination (Join-Path $destination 'platforms') -Force
    }
    # Include transitive UCRT64 DLL dependencies of the CLI and Qt plugins too.
    $queue = New-Object 'System.Collections.Generic.Queue[string]'
    Get-ChildItem -LiteralPath $destination -Recurse -File | Where-Object { $_.Extension -in '.exe','.dll' } | ForEach-Object { $queue.Enqueue($_.FullName) }
    $seen = @{}
    while ($queue.Count -gt 0) {
        $binary = $queue.Dequeue()
        if ($seen.ContainsKey($binary)) { continue }
        $seen[$binary] = $true
        $dependencies = & (Join-Path $QtPrefix 'bin/objdump.exe') -p $binary
        if ($LASTEXITCODE -ne 0) { throw "Could not inspect dependencies: $binary" }
        foreach ($line in $dependencies) {
            if ($line -match 'DLL Name:\s*(\S+)') {
                $name = $Matches[1]
                $source = Join-Path $QtPrefix "bin/$name"
                $target = Join-Path $destination $name
                if ((Test-Path -LiteralPath $source) -and -not (Test-Path -LiteralPath $target)) {
                    Copy-Item -LiteralPath $source -Destination $target
                    $queue.Enqueue($target)
                }
            }
        }
    }
    $docs = Join-Path $destination 'docs'
    New-Item -ItemType Directory -Path $docs -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'docs/medvessel_learning_guide.html') -Destination $docs -Force
    $researchReport = Join-Path $repositoryRoot 'docs/traditional_coronary_report.html'
    if (Test-Path -LiteralPath $researchReport) {
        Copy-Item -LiteralPath $researchReport -Destination $docs -Force
    }
    foreach ($reportName in @('segmentation_optimization.html', 'evaluation_and_industry.html', 'nnunet_hybrid.html')) {
        $reportPath = Join-Path $repositoryRoot "docs/$reportName"
        if (Test-Path -LiteralPath $reportPath) {
            Copy-Item -LiteralPath $reportPath -Destination $docs -Force
        }
    }
    $samples = Join-Path $destination 'samples'
    & (Join-Path $destination 'medvessel_phantom.exe') $samples
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate bundled synthetic sample.' }
    [IO.File]::WriteAllText((Join-Path $destination 'qt.conf'), "[Paths]`nPrefix=.`nPlugins=.`n")
    Write-Host "Desktop build: $destination/medvessel_gui.exe"
} finally { $env:PATH = $savedPath }
