[CmdletBinding()]
param([switch] $SkipBuild, [string] $InnoCompiler)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
& pwsh -NoProfile -File (Join-Path $PSScriptRoot 'make_sfx.ps1') -SkipBuild -OutputName 'PDFReader-Portable.exe'
if ($LASTEXITCODE -ne 0) { throw 'Portable packaging failed.' }
$installerArgs = @('-NoProfile','-File',(Join-Path $PSScriptRoot 'make_installer.ps1'),'-SkipBuild')
if ($InnoCompiler) { $installerArgs += @('-InnoCompiler',$InnoCompiler) }
& pwsh @installerArgs
if ($LASTEXITCODE -ne 0) { throw 'Installer packaging failed.' }
$dist = Join-Path $root 'dist'
$names = @('PDFReader-Setup.exe','PDFReader-Portable.exe')
$checksums = foreach ($name in $names) {
    (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $name
}
Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Value $checksums -Encoding utf8NoBOM
Write-Host "Release files: $dist"
foreach ($name in ($names + 'SHA256SUMS.txt')) { Get-Item -LiteralPath (Join-Path $dist $name) | Select-Object Name,Length }
