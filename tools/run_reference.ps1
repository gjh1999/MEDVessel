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
    $env:OMP_NUM_THREADS = '4'
    $python = Join-Path $repositoryRoot '.venv/Scripts/python.exe'
    Invoke-Checked $python @('tools/convert_reference.py', 'datasets/vmtk/aorta.mha', 'datasets/vmtk/aorta.nii.gz')
    Invoke-Checked $python @('tools/convert_reference.py', 'datasets/vmtk/aorta-final-levelset.mha', 'datasets/vmtk/aorta-final-levelset.nii.gz')
    $result = 'build/reference-results/vmtk-aorta-roi'
    Invoke-Checked (Join-Path $BuildDirectory 'medvessel_cli.exe') @(
        'datasets/vmtk/aorta.nii.gz', $result, '--roi', '40,55,0,80,150,34', '--scales', '1,2,3,4,5',
        '--c', '200', '--low', '0.2', '--high', '0.3', '--seed', '49,32,19',
        '--min-intensity', '1600', '--max-intensity', '2400', '--min-component', '100', '--prune-mm', '3'
    )
    Invoke-Checked (Join-Path $BuildDirectory 'medvessel_evaluate.exe') @(
        "$result/mask.nii.gz", 'datasets/vmtk/aorta-final-levelset.nii.gz', "$result/agreement.json",
        '--reference-levelset-negative', '--align-reference'
    )
    if ($PlotPython) {
        $export = Join-Path $BuildDirectory 'medvessel_export.exe'
        Invoke-Checked $export @('datasets/vmtk/aorta.nii.gz', 'build/reference-plots/source', '--roi', '40,55,0,80,150,34')
        Invoke-Checked $export @('datasets/vmtk/aorta-final-levelset.nii.gz', 'build/reference-plots/reference', '--roi', '40,55,0,80,150,34')
        Invoke-Checked $export @("$result/vesselness.nii.gz", 'build/reference-plots/vesselness')
        Invoke-Checked $export @("$result/mask.nii.gz", 'build/reference-plots/mask')
        $env:MPLCONFIGDIR = Join-Path $repositoryRoot 'build/matplotlib'
        Invoke-Checked $PlotPython @('tools/plot_reference.py', 'build/reference-plots', "$result/comparison.png")
    }
} finally {
    $env:OMP_NUM_THREADS = $savedThreads
    $env:MPLCONFIGDIR = $savedMatplotlibConfig
    Pop-Location
}
