# Build a single-file PDFReader.exe.
#
# Stages the built program plus every runtime it needs, embeds them as resources
# in a small Win32 launcher, and links the result. Running the produced exe
# unpacks once into %LOCALAPPDATA%\PDFReader and launches from there.
#
#   pwsh -NoProfile -File tools/make_sfx.ps1
#   pwsh -NoProfile -File tools/make_sfx.ps1 -SkipBuild
#
# Output: dist\PDFReader.exe
[CmdletBinding()]
param(
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$root  = Split-Path -Parent $PSScriptRoot          # ...\pdfreader
$build = Join-Path $root 'build'
$mingw = Join-Path $root 'tools\Tools\mingw1310_64\bin'
$dist  = Join-Path $root 'dist'
$stage = Join-Path $root 'build\sfx-stage'

if (-not $SkipBuild) {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

if (-not (Test-Path (Join-Path $build 'pdfreader.exe'))) {
    throw "missing $build\pdfreader.exe"
}
$gpp      = Join-Path $mingw 'g++.exe'
$windres  = Join-Path $mingw 'windres.exe'
foreach ($tool in $gpp, $windres) {
    if (-not (Test-Path $tool)) { throw "missing $tool" }
}

# ---- stage the payload ----------------------------------------------------
# Everything in build\ that the reader needs at runtime: the program, the Qt
# runtime it was linked against, the platform plugin windeployqt produced, the
# MinGW runtime, and PDFium. Helper executables (toc_build, probes) stay out.
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$wanted = @(
    'pdfreader.exe'
    'pdfium.dll'
    'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6PrintSupport.dll', 'Qt6Network.dll'
    'libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll'
)
$payload = @()
foreach ($name in $wanted) {
    $src = Join-Path $build $name
    if (Test-Path $src) {
        Copy-Item $src $stage -Force
        $payload += $name
    } else {
        Write-Warning "missing $name (skipped)"
    }
}

# Qt plugins live in subdirectories; keep their relative paths.
foreach ($pluginDir in 'platforms', 'styles', 'imageformats') {
    $srcDir = Join-Path $build $pluginDir
    if (-not (Test-Path $srcDir)) { continue }
    Get-ChildItem $srcDir -Filter *.dll | ForEach-Object {
        $rel = "$pluginDir\$($_.Name)"
        $destDir = Join-Path $stage $pluginDir
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null
        Copy-Item $_.FullName $destDir -Force
        $payload += $rel
    }
}

if ($payload.Count -eq 0) { throw 'nothing to package' }

# ---- generate the resource script ----------------------------------------
$rcPath    = Join-Path $stage 'payload.rc'
$namesPath = Join-Path $stage 'names.txt'

# The name list is read back in resource-id order, so the two must agree.
Set-Content -Path $namesPath -Value ($payload -join "`n") -NoNewline -Encoding ascii

$stamp = "pdfreader-sfx " + (Get-Date -Format 'yyyyMMddHHmmss') + " " + $payload.Count
Set-Content -Path (Join-Path $stage 'stamp.txt') -Value $stamp -NoNewline -Encoding ascii

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine('1000 RCDATA "names.txt"')
[void]$sb.AppendLine('1001 RCDATA "stamp.txt"')
for ($i = 0; $i -lt $payload.Count; $i++) {
    $rel = $payload[$i] -replace '\\', '\\\\'
    [void]$sb.AppendLine("$($i + 1) RCDATA `"$rel`"")
}
Set-Content -Path $rcPath -Value $sb.ToString() -Encoding ascii

# ---- compile --------------------------------------------------------------
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$outExe = Join-Path $dist 'PDFReader.exe'

Push-Location $stage
try {
    & $windres 'payload.rc' '-O' 'coff' '-o' 'payload.o'
    if ($LASTEXITCODE -ne 0) { throw 'windres failed' }

    & $gpp '-std=c++17' '-O2' '-municode' '-mwindows' '-static' '-static-libgcc' `
           '-o' $outExe (Join-Path $PSScriptRoot 'sfx\launcher.cpp') 'payload.o' `
           '-luser32'
    if ($LASTEXITCODE -ne 0) { throw 'launcher link failed' }
} finally {
    Pop-Location
}

$sizeMb = [math]::Round((Get-Item $outExe).Length / 1MB, 1)
Write-Host ""
Write-Host "packaged $($payload.Count) file(s) -> $outExe ($sizeMb MB)" -ForegroundColor Green
$payload | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
