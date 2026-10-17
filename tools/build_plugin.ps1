# Build a plugin independently: no reader source, PDFium or reader_ui linkage.
[CmdletBinding()]
param(
    [string] $Source,
    [string] $BuildDir,
    [string] $QtPrefix,
    [string] $Compiler,
    [string] $CMake,
    [string] $Ninja
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Source) { $Source = Join-Path $root 'plugins/reading_tools' }
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build/plugin-development' }
if (-not $QtPrefix) { $QtPrefix = Join-Path $root 'tools' }
if (-not $Compiler) { $Compiler = Join-Path $root 'tools/Tools/mingw1310_64/bin/g++.exe' }
if (-not $CMake) {
    $CMake = Join-Path $root '.venv/Scripts/cmake.exe'
    if (-not (Test-Path -LiteralPath $CMake)) { $CMake = (Get-Command cmake -ErrorAction Stop).Source }
}
if (-not $Ninja) {
    $Ninja = Join-Path $root '.venv/Scripts/ninja.exe'
    if (-not (Test-Path -LiteralPath $Ninja)) { $Ninja = (Get-Command ninja -ErrorAction Stop).Source }
}
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Pass -Compiler with the matching MinGW g++.exe path.' }
$env:Path = (Split-Path -Parent $Compiler) + ';' + (Join-Path $QtPrefix 'bin') + ';' + $env:Path
& $CMake -G Ninja -S $Source -B $BuildDir -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=$Compiler" "-DCMAKE_PREFIX_PATH=$QtPrefix" "-DCMAKE_MAKE_PROGRAM=$Ninja"
if ($LASTEXITCODE -ne 0) { throw 'Plugin configuration failed.' }
& $CMake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw 'Plugin build failed.' }
Get-ChildItem -LiteralPath (Join-Path $BuildDir 'reader-plugins') -File | Select-Object Name,FullName,Length
