[CmdletBinding()]
param(
    [switch] $SkipBuild,
    [string] $InnoCompiler,
    [switch] $InstallerTest
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
if (-not $InnoCompiler) {
    $candidates = @(
        (Join-Path $root 'build/release-tools/inno/ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 7/ISCC.exe')
    )
    $InnoCompiler = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $InnoCompiler -or -not (Test-Path -LiteralPath $InnoCompiler)) {
    throw 'Install Inno Setup 6.7+ or pass -InnoCompiler <path to ISCC.exe>.'
}
$stage = Join-Path $root 'build/installer-stage'
$payload = @(& (Join-Path $PSScriptRoot 'stage_runtime.ps1') -Destination $stage)
$webviewSetup = Join-Path $root 'build/release-tools/MicrosoftEdgeWebview2Setup.exe'
if (-not (Test-Path -LiteralPath $webviewSetup)) {
    New-Item -ItemType Directory -Force -Path (Split-Path $webviewSetup) | Out-Null
    Invoke-WebRequest 'https://go.microsoft.com/fwlink/p/?LinkId=2124703' -OutFile $webviewSetup
}
$signature = Get-AuthenticodeSignature -LiteralPath $webviewSetup
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    throw 'The WebView2 bootstrapper must have a valid Microsoft signature.'
}
$version = (Get-Item (Join-Path $stage 'pdfreader.exe')).VersionInfo.ProductVersion
$version = ($version -split '\.')[0..2] -join '.'
$arguments = @('/Qp', "/DProjectRoot=$root", "/DPayloadDir=$stage", "/DAppVersion=$version", "/DWebViewSetup=$webviewSetup")
if ($InstallerTest) { $arguments += '/DInstallerTest=1' }
$arguments += (Join-Path $PSScriptRoot 'installer/pdfreader.iss')
& $InnoCompiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
Write-Host "`nPackaged $($payload.Count) files for PDF Reader $version."
