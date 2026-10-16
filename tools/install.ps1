# Install PDF Reader for the current user.
#
# Puts the program somewhere stable, adds a Start Menu entry, and registers it
# as a PDF handler so it shows up in "Open with" and in Windows Settings >
# Default apps.
#
#   pwsh -NoProfile -File tools/install.ps1
#   pwsh -NoProfile -File tools/install.ps1 -SetDefault   # also try to take over .pdf
#
# Everything is written under HKCU, so no administrator rights are needed and
# nothing outside your own user account is touched.
[CmdletBinding()]
param(
    # Try to make it the default .pdf handler. Windows 10+ guards that choice
    # with a per-user hash it only lets the Settings UI write, so this can fail
    # by design; the script then tells you which two clicks finish the job.
    [switch] $SetDefault,
    # Where to install. Defaults to %LOCALAPPDATA%\Programs\PDFReader.
    [string] $InstallDir
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$root = Split-Path -Parent $PSScriptRoot
$sfx  = Join-Path $root 'dist\PDFReader.exe'
$dev  = Join-Path $root 'build\pdfreader.exe'

if (-not $InstallDir) {
    $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\PDFReader'
}

# Prefer the single-file build: it needs no sibling DLLs.
$useSfx = Test-Path $sfx
if (-not $useSfx -and -not (Test-Path $dev)) {
    throw "没有可安装的构建。先运行 tools/make_sfx.ps1 或 tools/build.ps1。"
}

Write-Host "`n=== 1. 复制程序到 $InstallDir ===" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

if ($useSfx) {
    $target = Join-Path $InstallDir 'PDFReader.exe'
    Copy-Item $sfx $target -Force
    Write-Host "  PDFReader.exe  (单文件，$([math]::Round((Get-Item $target).Length/1MB,1)) MB)"
} else {
    # Developer build: copy the program plus every DLL beside it.
    $target = Join-Path $InstallDir 'pdfreader.exe'
    Copy-Item $dev $target -Force
    $build = Split-Path -Parent $dev
    foreach ($dll in (Get-ChildItem $build -Filter '*.dll')) {
        Copy-Item $dll.FullName $InstallDir -Force
    }
    foreach ($sub in 'platforms', 'styles', 'imageformats') {
        $srcSub = Join-Path $build $sub
        if (Test-Path $srcSub) {
            Copy-Item $srcSub $InstallDir -Recurse -Force
        }
    }
    Write-Host "  pdfreader.exe + 运行时 DLL" -ForegroundColor DarkGray
}

$exe = Join-Path $InstallDir $(if ($useSfx) { 'PDFReader.exe' } else { 'pdfreader.exe' })
$cmd = "`"$exe`" `"%1`""

Write-Host "`n=== 2. 开始菜单快捷方式 ===" -ForegroundColor Cyan
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$lnk = Join-Path $startMenu 'PDF Reader.lnk'
try {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($lnk)
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = $InstallDir
    $shortcut.Description = 'PDF Reader'
    $shortcut.Save()
    Write-Host "  $lnk"
} catch {
    Write-Warning "  创建快捷方式失败：$($_.Exception.Message)"
}

Write-Host "`n=== 3. 注册为 PDF 打开方式 ===" -ForegroundColor Cyan
$progId = 'PDFReader.pdf'

# ProgID: what actually gets launched.
$k = "HKCU:\Software\Classes\$progId"
New-Item -Path $k -Force -Value 'PDF 文档' | Out-Null
New-Item -Path "$k\DefaultIcon" -Force -Value "`"$exe`",0" | Out-Null
New-Item -Path "$k\shell\open\command" -Force -Value $cmd | Out-Null
Write-Host "  ProgID          $progId"

# Offer it in the "Open with" menu without stealing the current default.
$k = 'HKCU:\Software\Classes\.pdf\OpenWithProgids'
New-Item -Path $k -Force | Out-Null
New-ItemProperty -Path $k -Name $progId -Value '' -PropertyType String -Force | Out-Null
Write-Host "  OpenWithProgids .pdf"

# Applications entry: makes it a real, named application to Explorer.
$k = 'HKCU:\Software\Classes\Applications\PDFReader.exe'
New-Item -Path $k -Force | Out-Null
New-ItemProperty -Path $k -Name 'FriendlyAppName' -Value 'PDF Reader' -Force | Out-Null
New-ItemProperty -Path $k -Name 'DefaultIcon' -Value "`"$exe`",0" -Force | Out-Null
New-Item -Path "$k\shell\open\command" -Force -Value $cmd | Out-Null
New-Item -Path "$k\SupportedTypes" -Force | Out-Null
New-ItemProperty -Path "$k\SupportedTypes" -Name '.pdf' -Value '' -PropertyType String -Force | Out-Null
Write-Host "  Applications\PDFReader.exe"

# Capabilities: makes it appear in Settings > Apps > Default apps.
$k = 'HKCU:\Software\PDFReader\Capabilities'
New-Item -Path $k -Force | Out-Null
New-ItemProperty -Path $k -Name 'ApplicationName' -Value 'PDF Reader' -Force | Out-Null
New-ItemProperty -Path $k -Name 'ApplicationDescription' `
    -Value '带自动目录重建的 PDF 阅读器' -Force | Out-Null
New-Item -Path "$k\FileAssociations" -Force | Out-Null
New-ItemProperty -Path "$k\FileAssociations" -Name '.pdf' -Value $progId -Force | Out-Null
New-ItemProperty -Path 'HKCU:\Software\RegisteredApplications' -Name 'PDFReader' `
    -Value 'Software\PDFReader\Capabilities' -Force | Out-Null
Write-Host "  RegisteredApplications"

Write-Host "`n=== 4. 通知资源管理器刷新 ===" -ForegroundColor Cyan
try {
    Add-Type -Namespace Shell -Name Notify -MemberDefinition @'
[DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, int flags, IntPtr item1, IntPtr item2);
'@ -ErrorAction SilentlyContinue
    [Shell.Notify]::SHChangeNotify(0x08000000, 0x0000, [IntPtr]::Zero, [IntPtr]::Zero)
    Write-Host "  已刷新关联缓存"
} catch {
    Write-Host "  （跳过，通常不影响）" -ForegroundColor DarkGray
}

$current = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\UserChoice' -ErrorAction SilentlyContinue).ProgId
Write-Host "`n当前 .pdf 默认程序: $(if ($current) { $current } else { '(未设置)' })" -ForegroundColor DarkGray

if ($SetDefault -and $current -ne $progId) {
    Write-Host "`n=== 5. 尝试设为默认 ===" -ForegroundColor Cyan
    # Windows 10+ protects the default-handler choice with a hash only the
    # Settings UI can produce, so writing UserChoice directly is refused. The
    # honest move is to open the right page and let the user click.
    try {
        $k = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\UserChoice'
        Remove-Item $k -Recurse -Force -ErrorAction Stop
        Write-Host "  已清除旧的 UserChoice，Windows 会在下次打开 .pdf 时询问"
    } catch {
        Write-Host "  Windows 拒绝了直接写入（这是设计如此）" -ForegroundColor Yellow
    }
    Start-Process 'ms-settings:defaultapps'
    Write-Host "  已打开「默认应用」设置页 —— 搜 PDF 后选择 PDF Reader (2 次点击)" -ForegroundColor Yellow
}

Write-Host "`n安装完成。" -ForegroundColor Green
Write-Host "  程序位置 : $exe"
Write-Host "  使用方式 : 右键任意 PDF -> 打开方式 -> PDF Reader"
Write-Host "            或开始菜单搜索 `"PDF Reader`""
Write-Host "  卸载     : pwsh -NoProfile -File tools/uninstall.ps1`n"
