# Generates a MinGW-compatible import library (libpdfium.a) from the
# MSVC-built pdfium.dll shipped by bblanchon/pdfium-binaries.
#
# Why: PDFium's official Windows binaries ship pdfium.dll + an MSVC import
# library (pdfium.dll.lib). GNU ld cannot link that directly, so we synthesize
# a .def file with gendef and build a MinGW import library with dlltool.
# The PDFium C API is extern "C", so the ABI is compatible.

$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent $PSScriptRoot          # D:\harness_test\pdfreader
$mingw = Join-Path $root 'tools\Tools\mingw1310_64\bin'
$bin   = Join-Path $root 'tools\pdfium\bin'
$lib   = Join-Path $root 'tools\pdfium\lib'

foreach ($tool in 'gendef.exe', 'dlltool.exe') {
    if (-not (Test-Path (Join-Path $mingw $tool))) { throw "missing $tool in $mingw" }
}
if (-not (Test-Path (Join-Path $bin 'pdfium.dll'))) { throw "missing pdfium.dll in $bin" }

Push-Location $bin
try {
    & (Join-Path $mingw 'gendef.exe') pdfium.dll
    if ($LASTEXITCODE -ne 0) { throw "gendef failed ($LASTEXITCODE)" }

    & (Join-Path $mingw 'dlltool.exe') -d pdfium.def -l (Join-Path $lib 'libpdfium.a') -D pdfium.dll
    if ($LASTEXITCODE -ne 0) { throw "dlltool failed ($LASTEXITCODE)" }
}
finally {
    Pop-Location
}

$implib = Join-Path $lib 'libpdfium.a'
Write-Host "import library: $implib ($((Get-Item $implib).Length) bytes)"
