# Delegate uninstall to Inno Setup's file log; preserve user data and other apps.
[CmdletBinding()]
param([string] $InstallDir, [switch] $Silent)
$ErrorActionPreference = 'Stop'
if (-not $InstallDir) { $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs/PDFReader' }
$InstallDir = [IO.Path]::GetFullPath($InstallDir)
$uninstaller = Join-Path $InstallDir 'uninstall/unins000.exe'
if (-not (Test-Path -LiteralPath $uninstaller)) {
    throw 'Native uninstaller not found. Upgrade an older script installation using PDFReader-Setup.exe first.'
}
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $uninstaller
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.ArgumentList.Add('/NORESTART')
if ($Silent) {
    $info.ArgumentList.Add('/VERYSILENT')
    $info.ArgumentList.Add('/SUPPRESSMSGBOXES')
}
$process = [Diagnostics.Process]::Start($info)
$process.WaitForExit()
$code = $process.ExitCode
$process.Dispose()
if ($code -ne 0) { throw "Uninstaller exited with code $code" }
