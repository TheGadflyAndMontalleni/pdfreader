# Undo tools/install.ps1.
#
# Removes the registry entries, the Start Menu shortcut, and the installed
# program files. Only touches what the installer created, all under HKCU.
#
#   pwsh -NoProfile -File tools/uninstall.ps1
[CmdletBinding()]
param(
    [string] $InstallDir
)

$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

if (-not $InstallDir) {
    $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\PDFReader'
}

Write-Host "`n=== 移除注册表项 ===" -ForegroundColor Cyan
$keys = @(
    'HKCU:\Software\Classes\PDFReader.pdf'
    'HKCU:\Software\Classes\Applications\PDFReader.exe'
    'HKCU:\Software\PDFReader'
)
foreach ($k in $keys) {
    if (Test-Path $k) {
        Remove-Item $k -Recurse -Force
        Write-Host "  已删除 $k"
    }
}

# These two are shared containers, so only remove our own value from them.
foreach ($pair in @(
        @{ Path = 'HKCU:\Software\Classes\.pdf\OpenWithProgids'; Name = 'PDFReader.pdf' }
        @{ Path = 'HKCU:\Software\RegisteredApplications';     Name = 'PDFReader' })) {
    if (Test-Path $pair.Path) {
        $props = (Get-Item $pair.Path).Property
        if ($props -contains $pair.Name) {
            Remove-ItemProperty -Path $pair.Path -Name $pair.Name -Force
            Write-Host "  已从 $($pair.Path) 移除 $($pair.Name)"
        }
    }
}

Write-Host "`n=== 移除快捷方式 ===" -ForegroundColor Cyan
$lnk = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\PDF Reader.lnk'
if (Test-Path $lnk) { Remove-Item $lnk -Force; Write-Host "  已删除 $lnk" }

Write-Host "`n=== 移除程序文件 ===" -ForegroundColor Cyan
if (Test-Path $InstallDir) {
    Remove-Item $InstallDir -Recurse -Force
    Write-Host "  已删除 $InstallDir"
}

# The launcher unpacks here on first run.
$unpacked = Join-Path $env:LOCALAPPDATA 'PDFReader'
if (Test-Path $unpacked) {
    Remove-Item $unpacked -Recurse -Force
    Write-Host "  已删除解压目录 $unpacked"
}

Write-Host "`n卸载完成。" -ForegroundColor Green
Write-Host "  你的阅读记录和最近文件仍保留在:" -ForegroundColor DarkGray
Write-Host "    $(Join-Path $env:LOCALAPPDATA 'pdfreader\settings.ini')" -ForegroundColor DarkGray
Write-Host "  需要的话手动删除该文件即可。`n" -ForegroundColor DarkGray
