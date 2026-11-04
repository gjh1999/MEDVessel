param(
    [string]$BuildDirectory = 'build/cmake',
    [string]$QtPrefix = '',
    [string]$NiftiSource = 'build/deps/nifti_clib-master',
    [switch]$EnableVtk,
    [switch]$Test,
    [int]$Parallel = 4
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
function Resolve-ProjectPath([string]$Value) {
    if ([IO.Path]::IsPathRooted($Value)) { return [IO.Path]::GetFullPath($Value) }
    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $Value))
}
if (-not $QtPrefix) {
    $localPrefix = Join-Path $repositoryRoot 'build/deps/msys64/ucrt64'
    if (Test-Path -LiteralPath (Join-Path $localPrefix 'bin/cmake.exe')) { $QtPrefix = $localPrefix }
    else { $QtPrefix = 'C:/msys64/ucrt64' }
}
$QtPrefix = Resolve-ProjectPath $QtPrefix
$BuildDirectory = Resolve-ProjectPath $BuildDirectory
$NiftiSource = Resolve-ProjectPath $NiftiSource
foreach ($tool in @('cmake','ninja','gcc','g++','ctest')) {
    if (-not (Test-Path -LiteralPath (Join-Path $QtPrefix "bin/$tool.exe"))) {
        throw "Missing $tool in $QtPrefix/bin. See docs/qt_next_steps.md."
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $NiftiSource 'niftilib/nifti1_io.c'))) {
    throw 'NIfTI source not found. Supply -NiftiSource; see docs/build_windows.md.'
}
$savedPath = $env:PATH
$savedPlugins = $env:QT_PLUGIN_PATH
$savedPlatformPlugins = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $env:PATH = (Join-Path $QtPrefix 'bin') + ';' + $env:PATH
    $env:QT_PLUGIN_PATH = Join-Path $QtPrefix 'share/qt6/plugins'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $env:QT_PLUGIN_PATH 'platforms'
    $cmake = Join-Path $QtPrefix 'bin/cmake.exe'
    $vtk = if ($EnableVtk) { 'ON' } else { 'OFF' }
    & $cmake -S $repositoryRoot -B $BuildDirectory -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$QtPrefix/bin/ninja.exe" `
        "-DCMAKE_CXX_COMPILER=$QtPrefix/bin/g++.exe" `
        "-DCMAKE_C_COMPILER=$QtPrefix/bin/gcc.exe" `
        '-DCMAKE_BUILD_TYPE=Release' '-DMEDVESSEL_BUILD_GUI=ON' `
        "-DMEDVESSEL_ENABLE_VTK=$vtk" "-DCMAKE_PREFIX_PATH=$QtPrefix" `
        "-DMEDVESSEL_NIFTI_SOURCE_DIR=$NiftiSource" `
        "-DZLIB_INCLUDE_DIR=$QtPrefix/include" "-DZLIB_LIBRARY=$QtPrefix/lib/libz.a"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmake --build $BuildDirectory --parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    if ($Test) {
        & (Join-Path $QtPrefix 'bin/ctest.exe') --test-dir $BuildDirectory --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }
} finally {
    $env:PATH = $savedPath
    $env:QT_PLUGIN_PATH = $savedPlugins
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $savedPlatformPlugins
}
