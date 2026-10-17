# Developer convenience wrapper for the same native installer shipped in Releases.
[CmdletBinding()]
param([string] $InstallDir, [switch] $SetDefault, [switch] $Silent)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$setup = Join-Path $root 'dist/PDFReader-Setup.exe'
if (-not (Test-Path -LiteralPath $setup)) {
    throw 'Build the native installer first: pwsh -File tools/make_release.ps1'
}
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $setup
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.ArgumentList.Add('/NORESTART')
if ($InstallDir) { $info.ArgumentList.Add('/DIR=' + [IO.Path]::GetFullPath($InstallDir)) }
if ($Silent) {
    $info.ArgumentList.Add('/VERYSILENT')
    $info.ArgumentList.Add('/SUPPRESSMSGBOXES')
}
$process = [Diagnostics.Process]::Start($info)
$process.WaitForExit()
$code = $process.ExitCode
$process.Dispose()
if ($code -ne 0) { throw "Installer exited with code $code" }
if ($SetDefault) { Start-Process 'ms-settings:defaultapps' }
