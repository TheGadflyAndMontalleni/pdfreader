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
    [switch] $SkipBuild,
    [string] $OutputName = 'PDFReader.exe'
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
# Share the exact runtime and license payload with the native installer.
$payload = @(& (Join-Path $PSScriptRoot 'stage_runtime.ps1') -Destination $stage)
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

# The packaged exe is the file the user actually double-clicks, so it needs the
# icon too; otherwise Explorer shows a generic blank executable.
$iconSrc = Join-Path $root 'src\app\app.ico'
if (Test-Path $iconSrc) {
    Copy-Item $iconSrc (Join-Path $stage 'launcher.ico') -Force
    [void]$sb.AppendLine('#include <windows.h>')
    [void]$sb.AppendLine('IDI_APP_ICON ICON "launcher.ico"')
} else {
    Write-Warning 'src\app\app.ico missing; the packaged exe will have no icon'
}

# Version block for the packaged exe. This is the file that ends up in the
# user's Programs folder and in Explorer, so its Properties > Details must not be
# blank -- verified by tools/verify.ps1.
[void]$sb.AppendLine(@'
VS_VERSION_INFO VERSIONINFO
    FILEVERSION     0,3,0,0
    PRODUCTVERSION  0,3,0,0
    FILEFLAGSMASK   VS_FFI_FILEFLAGSMASK
    FILEFLAGS       0
    FILEOS          VOS_NT_WINDOWS32
    FILETYPE        VFT_APP
    FILESUBTYPE     VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "080404B0"
        BEGIN
            VALUE "CompanyName",      "TheGadflyAndMontalleni"
            VALUE "FileDescription",  "PDF Reader"
            VALUE "FileVersion",      "0.3.0.0"
            VALUE "InternalName",     "PDFReader"
            VALUE "OriginalFilename", "PDFReader.exe"
            VALUE "ProductName",      "PDF Reader"
            VALUE "ProductVersion",   "0.3.0.0"
            VALUE "Comments",         "PDF reader with automatic table-of-contents reconstruction"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x804, 1200
    END
END
'@)
Set-Content -Path $rcPath -Value $sb.ToString() -Encoding ascii

# ---- compile --------------------------------------------------------------
New-Item -ItemType Directory -Force -Path $dist | Out-Null
if ([IO.Path]::GetFileName($OutputName) -ne $OutputName -or [IO.Path]::GetExtension($OutputName) -ne '.exe') {
    throw 'OutputName must be an .exe filename without a directory.'
}
$outExe = Join-Path $dist $OutputName

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
