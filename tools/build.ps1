# One-shot build: configures with the bundled MinGW/Qt/PDFium, builds, and
# deploys the runtime DLLs next to the executables so they run without any
# PATH setup.
#
# Usage:  pwsh -File tools\build.ps1
$ErrorActionPreference = 'Stop'

$root   = Split-Path -Parent $PSScriptRoot          # D:\harness_test\pdfreader
$mingw  = Join-Path $root 'tools\Tools\mingw1310_64\bin'
$qtbin  = Join-Path $root 'tools\bin'
$venv   = Join-Path $root '.venv\Scripts'

if (-not (Test-Path (Join-Path $mingw 'g++.exe')))  { throw "MinGW not found at $mingw" }
if (-not (Test-Path (Join-Path $venv  'cmake.exe'))) { throw "cmake not found at $venv" }

$env:Path = "$mingw;$qtbin;$venv;$env:Path"

cmake -G Ninja -S $root -B "$root\build" -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_CXX_COMPILER="$mingw\g++.exe" `
      -DCMAKE_PREFIX_PATH="$root/tools"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

cmake --build "$root\build"
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# Deploy runtime dependencies (MinGW runtime + Qt + PDFium).
$build = Join-Path $root 'build'
foreach ($dll in 'libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll') {
    $src = Join-Path $mingw $dll
    if (Test-Path $src) { Copy-Item $src $build -Force }
}
Copy-Item (Join-Path $root 'tools\pdfium\bin\pdfium.dll') $build -Force
$qtCore = Join-Path $qtbin 'Qt6Core.dll'
if (Test-Path $qtCore) { Copy-Item $qtCore $build -Force }

# The GUI app additionally needs the Qt platform plugin (qwindows.dll) and the
# Widgets/Gui libraries; windeployqt resolves that set for us.
$reader    = Join-Path $build 'pdfreader.exe'
$deployQt  = Join-Path $qtbin 'windeployqt.exe'
if ((Test-Path $reader) -and (Test-Path $deployQt)) {
    & $deployQt --release --no-translations --no-compiler-runtime `
                --no-system-d3d-compiler --no-opengl-sw $reader | Out-Null
}

Write-Host "`nbuild ok -> $build"
Get-ChildItem $build -Filter '*.exe' | ForEach-Object { "  $($_.Name)" }
