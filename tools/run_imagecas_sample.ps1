param(
    [string]$BuildDirectory = 'build/cmake',
    [string]$PlotPython = ''
)
$ErrorActionPreference = 'Stop'
function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $Executable ($LASTEXITCODE)" }
}
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$savedThreads = $env:OMP_NUM_THREADS
$savedMatplotlibConfig = $env:MPLCONFIGDIR
Push-Location $repositoryRoot
try {
    $python = Join-Path $repositoryRoot '.venv/Scripts/python.exe'
    Invoke-Checked $python @('tools/fetch_imagecas_sample.py')
    Invoke-Checked $python @('tools/fetch_imagecas_x_label.py')
    $case = 'build/imagecas-x-case1'
    Invoke-Checked (Join-Path $BuildDirectory 'medvessel_prepare_case.exe') @(
        'datasets/imagecas-sample/1.img.nii.gz', 'datasets/imagecas-x-sample/1.coronary.nii.gz', $case, '20'
    )
    $roi = Get-Content "$case/roi.json" -Raw | ConvertFrom-Json
    $seed = $roi.seed_voxel -join ','
    $env:OMP_NUM_THREADS = '4'
    Invoke-Checked (Join-Path $BuildDirectory 'medvessel_cli.exe') @(
        "$case/source.nii.gz", "$case/result", '--scales', '0.5,1,1.5,2', '--c', '100',
        '--low', '0.08', '--high', '0.2', '--seed', $seed, '--min-intensity', '100',
        '--max-intensity', '900', '--min-component', '20', '--prune-mm', '1'
    )
    Invoke-Checked (Join-Path $BuildDirectory 'medvessel_evaluate.exe') @(
        "$case/result/mask.nii.gz", "$case/reference.nii.gz", "$case/result/metrics.json"
    )
    $experiment = [ordered]@{
        case_id = '1'
        source_dataset = 'ImageCAS v3'
        reference_dataset = 'ImageCAS-X v2 (Zenodo 23562647)'
        reference_guided_roi = $true
        reference_guided_seed = $true
        evaluation_scope = '109x109x81 local development ROI; not held-out benchmark'
        preparation_manifest = 'build/imagecas-x-case1/roi.json'
    }
    $experimentPath = Join-Path $repositoryRoot "$case/result/experiment.json"
    [IO.File]::WriteAllText($experimentPath, ($experiment | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
    if ($PlotPython) {
        $export = Join-Path $BuildDirectory 'medvessel_export.exe'
        Invoke-Checked $export @("$case/source.nii.gz", 'build/imagecas-x-plots/source')
        Invoke-Checked $export @("$case/reference.nii.gz", 'build/imagecas-x-plots/reference')
        Invoke-Checked $export @("$case/result/vesselness.nii.gz", 'build/imagecas-x-plots/vesselness')
        Invoke-Checked $export @("$case/result/mask.nii.gz", 'build/imagecas-x-plots/mask')
        $env:MPLCONFIGDIR = Join-Path $repositoryRoot 'build/matplotlib'
        Invoke-Checked $PlotPython @(
            'tools/plot_reference.py', 'build/imagecas-x-plots', "$case/result/comparison.png", '--slice', "$($roi.seed_voxel[2])",
            '--reference-mask-positive', '--title', 'ImageCAS case 1 - ImageCAS-X v2 reference',
            '--note', 'Reference-guided ROI and seed; not a held-out benchmark', '--reference-label', 'ImageCAS-X v2 mask'
        )
    }
} finally {
    $env:OMP_NUM_THREADS = $savedThreads
    $env:MPLCONFIGDIR = $savedMatplotlibConfig
    Pop-Location
}
