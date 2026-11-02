param(
    [string]$BuildDirectory = 'build/cmake',
    [string]$QtPrefix = 'C:/msys64/ucrt64',
    [string]$Result = '',
    [string]$Config = ''
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $repositoryRoot $BuildDirectory }
$executable = Join-Path $BuildDirectory 'medvessel_gui.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'GUI has not been built. See docs/gui.md.' }
$savedPath = $env:PATH
$savedPlugins = $env:QT_PLUGIN_PATH
$savedPlatformPlugins = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $env:PATH = (Join-Path $QtPrefix 'bin') + ';' + $env:PATH
    $env:QT_PLUGIN_PATH = Join-Path $QtPrefix 'share/qt6/plugins'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $env:QT_PLUGIN_PATH 'platforms'
    $arguments = @()
    if ($Result) { $arguments += @('--result', [IO.Path]::GetFullPath($Result)) }
    if ($Config) { $arguments += @('--config', [IO.Path]::GetFullPath($Config)) }
    & $executable @arguments
} finally {
    $env:PATH = $savedPath
    $env:QT_PLUGIN_PATH = $savedPlugins
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $savedPlatformPlugins
}
