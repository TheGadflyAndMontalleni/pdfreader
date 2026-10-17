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
    [string] $InstallDir,
    # Shown under Settings > Apps.
    [string] $Version = '0.1.0'
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

# New-Item -Force on an existing registry key WIPES its values -- the same way
# it truncates a file. Using it on a key shared with other applications silently
# destroys their registrations. (This actually happened: it cleared the nine
# "Open with" candidates that PdfGear, SumatraPDF, calibre and others had put in
# .pdf\OpenWithProgids.) Keys are only ever created when missing.
function New-RegKey {
    param([string] $Path, [string] $DefaultValue)
    if (Test-Path $Path) {
        if ($null -ne $DefaultValue) {
            Set-ItemProperty -Path $Path -Name '(Default)' -Value $DefaultValue
        }
        return
    }
    if ($null -ne $DefaultValue) {
        New-Item -Path $Path -Force -Value $DefaultValue | Out-Null
    } else {
        New-Item -Path $Path -Force | Out-Null
    }
}

# ProgID: what actually gets launched.
$k = "HKCU:\Software\Classes\$progId"
New-RegKey $k 'PDF 文档'
New-RegKey "$k\DefaultIcon" "`"$exe`",0"
New-RegKey "$k\shell\open\command" $cmd
Write-Host "  ProgID          $progId"

# Offer it in the "Open with" menu without stealing the current default.
#
# This key is SHARED: every PDF application on the machine adds its own value
# here. Snapshot the existing entries first, write ours through the .NET API
# (which cannot clobber siblings), then prove nothing was lost.
$sharedPath = 'Software\Classes\.pdf\OpenWithProgids'

# GetValueNames() is the only reliable way to enumerate registry VALUES.
# (Get-Item $key).PSObject.Properties returns the .NET object's own properties
# -- Property, SubKeyCount, View, Handle, ValueCount, Name -- so comparing those
# always reports "nothing lost" no matter what actually happened.
function Get-RegValueNames([string] $Path) {
    $reg = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($Path)
    if (-not $reg) { return @() }
    try { return @($reg.GetValueNames()) } finally { $reg.Close() }
}

$sharedBefore = Get-RegValueNames $sharedPath

$sharedKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($sharedPath)
$sharedKey.SetValue($progId, [byte[]]::new(0), [Microsoft.Win32.RegistryValueKind]::None)
$sharedKey.Close()

$sharedAfter = Get-RegValueNames $sharedPath
$lost = @($sharedBefore | Where-Object { $sharedAfter -notcontains $_ })
if ($lost.Count -gt 0) {
    Write-Warning "  !! 弄丢了 $($lost.Count) 个已有候选，正在恢复: $($lost -join ', ')"
    $fix = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($sharedPath, $true)
    foreach ($name in $lost) {
        $fix.SetValue($name, [byte[]]::new(0), [Microsoft.Win32.RegistryValueKind]::None)
    }
    $fix.Close()
    throw "安装中止：共享注册表键的内容未能保住，原有 $($lost.Count) 个条目已恢复。"
}
Write-Host "  OpenWithProgids .pdf（现有 $($sharedAfter.Count) 个候选；原有 $($sharedBefore.Count) 个全部保留）"

# Applications entry: makes it a real, named application to Explorer.
$k = 'HKCU:\Software\Classes\Applications\PDFReader.exe'
New-RegKey $k
New-ItemProperty -Path $k -Name 'FriendlyAppName' -Value 'PDF Reader' -Force | Out-Null
New-ItemProperty -Path $k -Name 'DefaultIcon' -Value "`"$exe`",0" -Force | Out-Null
New-RegKey "$k\shell\open\command" $cmd
New-RegKey "$k\SupportedTypes"
New-ItemProperty -Path "$k\SupportedTypes" -Name '.pdf' -Value '' -PropertyType String -Force | Out-Null
Write-Host "  Applications\PDFReader.exe"

# Capabilities: makes it appear in Settings > Apps > Default apps.
$k = 'HKCU:\Software\PDFReader\Capabilities'
New-RegKey $k
New-ItemProperty -Path $k -Name 'ApplicationName' -Value 'PDF Reader' -Force | Out-Null
New-ItemProperty -Path $k -Name 'ApplicationDescription' `
    -Value '带自动目录重建的 PDF 阅读器' -Force | Out-Null
New-RegKey "$k\FileAssociations"
New-ItemProperty -Path "$k\FileAssociations" -Name '.pdf' -Value $progId -Force | Out-Null
New-RegKey 'HKCU:\Software\RegisteredApplications'
New-ItemProperty -Path 'HKCU:\Software\RegisteredApplications' -Name 'PDFReader' `
    -Value 'Software\PDFReader\Capabilities' -Force | Out-Null
Write-Host "  RegisteredApplications"

# Uninstall entry: this is what makes Windows treat it as an installed program
# rather than a stray executable -- it shows up under Settings > Apps with the
# icon, version and a working uninstall button.
$k = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PDFReader'
New-Item -Path $k -Force | Out-Null
New-ItemProperty -Path $k -Name 'DisplayName'     -Value 'PDF Reader' -Force | Out-Null
New-ItemProperty -Path $k -Name 'DisplayVersion'  -Value $Version -Force | Out-Null
New-ItemProperty -Path $k -Name 'Publisher'       -Value 'TheGadflyAndMontalleni' -Force | Out-Null
New-ItemProperty -Path $k -Name 'DisplayIcon'     -Value "`"$exe`",0" -Force | Out-Null
New-ItemProperty -Path $k -Name 'InstallLocation' -Value $InstallDir -Force | Out-Null
foreach ($number in 'NoModify', 'NoRepair') {
    New-ItemProperty -Path $k -Name $number -Value 1 -PropertyType DWord -Force | Out-Null
}
$sizeKb = [int](((Get-ChildItem $InstallDir -Recurse -File |
                  Measure-Object Length -Sum).Sum) / 1KB)
New-ItemProperty -Path $k -Name 'EstimatedSize' -Value $sizeKb -PropertyType DWord -Force | Out-Null

# The uninstall command must survive the source tree being deleted, so the
# script is copied next to the program.
$uninstallScript = Join-Path $PSScriptRoot 'uninstall.ps1'
if (Test-Path $uninstallScript) {
    $copy = Join-Path $InstallDir 'uninstall.ps1'
    Copy-Item $uninstallScript $copy -Force
    $command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$copy`""
    New-ItemProperty -Path $k -Name 'UninstallString' -Value $command -Force | Out-Null
    New-ItemProperty -Path $k -Name 'QuietUninstallString' -Value $command -Force | Out-Null
}
Write-Host "  已注册到「设置 → 应用」($sizeKb KB)"

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

# Replacing an exe at the same path leaves the old icon in Explorer's cache, so
# an update would keep showing the previous (or blank) icon. Clearing the cache
# is what makes the new one appear without a reboot or a rename.
$explorerCache = Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\Explorer'
$removed = 0
if (Test-Path $explorerCache) {
    Get-ChildItem $explorerCache -Filter 'iconcache_*.db' -ErrorAction SilentlyContinue |
        ForEach-Object {
            try { Remove-Item $_.FullName -Force -ErrorAction Stop; $removed++ } catch {}
        }
}
$legacy = Join-Path $env:LOCALAPPDATA 'IconCache.db'
if (Test-Path $legacy) {
    try { Remove-Item $legacy -Force -ErrorAction Stop; $removed++ } catch {}
}
if ($removed -gt 0) {
    Write-Host "  已清除 $removed 个图标缓存文件" -ForegroundColor DarkGray
    $ie = Join-Path $env:WINDIR 'System32\ie4uinit.exe'
    if (Test-Path $ie) { & $ie -show 2>$null | Out-Null }
    Write-Host "  若仍显示旧图标：注销一次，或重启资源管理器" -ForegroundColor DarkGray
} else {
    Write-Host "  （图标缓存无需清理）" -ForegroundColor DarkGray
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
