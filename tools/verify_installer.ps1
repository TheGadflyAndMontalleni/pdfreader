# Compile the same installer with isolated registry/shortcut destinations, then
# exercise install, upgrade, installed-reader operation, and uninstall.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Pdf,
    [switch] $SkipBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'make_installer.ps1') -SkipBuild -InstallerTest
    if ($LASTEXITCODE -ne 0) { throw 'Could not build the isolated installer.' }
}
$testRoot = Join-Path $root ('build/installer-smoke-' + [Guid]::NewGuid().ToString('N'))
$install = Join-Path $testRoot 'installed'
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
$testRegistry = 'HKCU:\Software\PDFReaderInstallerTest'
if (Test-Path -LiteralPath $testRegistry) { throw 'An existing installer test is using the isolated registry root.' }
$classes = "$testRegistry\Classes"
$shared = "$classes\.pdf\OpenWithProgids"
$registered = "$testRegistry\RegisteredApplications"
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TheGadflyAndMontalleni.PDFReader.InstallerTest_is1'
$script:Checks = 0
function Check([bool] $Condition, [string] $Message) {
    $script:Checks++
    if (-not $Condition) { throw "FAIL $Message" }
    Write-Host "PASS $Message"
}
function RegistrySnapshot([string] $Path) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($Path)
    if (-not $key) { return '<absent>' }
    try {
        $items = foreach ($name in ($key.GetValueNames() | Sort-Object)) {
            $value = $key.GetValue($name)
            if ($value -is [byte[]]) { $value = [Convert]::ToBase64String($value) }
            "$name=$value"
        }
        return $items -join "`n"
    } finally { $key.Close() }
}
function Run([string] $Program, [string[]] $Arguments, [string] $LogName) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Program
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    Set-Content -LiteralPath (Join-Path $testRoot $LogName) -Value $text -Encoding utf8
    $result = @{ Code = $process.ExitCode; Text = $text }
    $process.Dispose()
    return $result
}
$beforeOpenWith = RegistrySnapshot 'Software\Classes\.pdf\OpenWithProgids'
$beforeRegistered = RegistrySnapshot 'Software\RegisteredApplications'
$beforeDefault = RegistrySnapshot 'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\UserChoice'
$env:PDFREADER_SETTINGS = Join-Path $testRoot 'user-data/settings.ini'
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_PLUGIN_PATH = Join-Path $root 'tools/plugins'
$env:QT_SCALE_FACTOR = '2'
$setup = Join-Path $root 'dist/PDFReader-Setup-Test.exe'
$uninstaller = Join-Path $install 'uninstall/unins000.exe'
Write-Host "Installer test workspace: $testRoot"
try {
    New-Item -Path $shared -Force | Out-Null
    New-ItemProperty -Path $shared -Name 'OtherReader.pdf' -Value '' -PropertyType String | Out-Null
    New-Item -Path $registered -Force | Out-Null
    New-ItemProperty -Path $registered -Name 'OtherReader' -Value 'Software\OtherReader\Capabilities' | Out-Null
    $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/DIR=$install","/LOG=$testRoot/install.log")
    $r = Run $setup $arguments 'install-output.log'
    Check ($r.Code -eq 0) 'native installer completes without elevation or a restart'
    $payload = Join-Path $root 'build/installer-stage'
    $matching = $true
    foreach ($file in (Get-ChildItem -LiteralPath $payload -Recurse -File)) {
        $destination = Join-Path $install ([IO.Path]::GetRelativePath($payload, $file.FullName))
        if (-not (Test-Path -LiteralPath $destination) -or
            (Get-FileHash -LiteralPath $destination).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { $matching = $false }
    }
    Check $matching 'all installed binaries, plugins, notices and licenses match the release payload'
    $exe = Join-Path $install 'pdfreader.exe'
    $command = (Get-Item -LiteralPath "$classes\PDFReader.pdf\shell\open\command").GetValue('')
    Check ($command -eq "`"$exe`" `"%1`"") 'PDF Open with command quotes both executable and PDF path'
    Check ((Get-Item -LiteralPath "$classes\PDFReader.pdf").GetValue('') -eq 'PDF 文档') 'installer preserves Chinese text in application registration'
    Check ((Get-Item -LiteralPath $shared).GetValueNames() -contains 'OtherReader.pdf') 'installation preserves other PDF handlers'
    Check ((Get-Item -LiteralPath $shared).GetValueNames() -contains 'PDFReader.pdf') 'installation adds its own PDF handler'
    Check ((Get-ItemProperty -LiteralPath $registered).PDFReader -eq 'Software\PDFReaderInstallerTest\Capabilities') 'application capabilities are registered'
    $shell = New-Object -ComObject WScript.Shell
    $start = $shell.CreateShortcut((Join-Path $install 'test-shortcuts/PDF Reader.lnk'))
    $desktop = $shell.CreateShortcut((Join-Path $install 'test-shortcuts/PDF Reader Desktop.lnk'))
    Check ($start.TargetPath -eq $exe -and $desktop.TargetPath -eq $exe) 'start-menu and desktop shortcuts target the installed reader'
    $payloadVersion = (Get-Item -LiteralPath (Join-Path $payload 'pdfreader.exe')).VersionInfo.ProductVersion
    $installerVersion = ($payloadVersion -split '\.')[0..2] -join '.'
    Check ((Get-ItemProperty -LiteralPath $uninstallKey).DisplayVersion -eq $installerVersion -and
        (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -eq $payloadVersion) 'Windows receives version information and an uninstall entry'
    $r = Run $exe @('--selftest',$Pdf) 'installed-reader.log'
    Check ($r.Code -eq 0 -and $r.Text -match 'SELFTEST PASS') 'installed reader passes its real 200-percent-DPI self-test'
    Check (@(Get-ChildItem (Join-Path $testRoot 'user-data/toc-cache') -Filter '*.json').Count -gt 0) 'installed reader starts its isolated directory worker and saves the result'
    $userData = Get-Content -LiteralPath $env:PDFREADER_SETTINGS -Raw
    Set-Content -LiteralPath (Join-Path $install 'LICENSE') -Value 'old version placeholder'
    $r = Run $setup $arguments 'upgrade-output.log'
    Check ($r.Code -eq 0 -and (Get-FileHash (Join-Path $install 'LICENSE')).Hash -eq (Get-FileHash (Join-Path $payload 'LICENSE')).Hash) 'reinstall upgrades existing payload files'
    Check ((Get-Content -LiteralPath $env:PDFREADER_SETTINGS -Raw) -eq $userData) 'upgrade preserves the reader settings file'
    $userFile = Join-Path $install 'my-document.txt'
    Set-Content -LiteralPath $userFile -Value 'This user-added file must survive uninstall.'
    $r = Run $uninstaller @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=$testRoot/uninstall.log") 'uninstall-output.log'
    Check ($r.Code -eq 0 -and -not (Test-Path -LiteralPath $exe)) 'native uninstaller removes the installed application'
    Check (-not (Test-Path -LiteralPath $uninstallKey)) 'uninstall removes its Windows application entry'
    Check (-not (Test-Path -LiteralPath "$classes\PDFReader.pdf") -and
        ((Get-Item -LiteralPath $shared).GetValueNames() -contains 'OtherReader.pdf') -and
        -not ((Get-Item -LiteralPath $shared).GetValueNames() -contains 'PDFReader.pdf')) 'uninstall removes only its own PDF registration'
    Check (Test-Path -LiteralPath $userFile) 'uninstall preserves user-added files'
    Check ((Get-Content -LiteralPath $env:PDFREADER_SETTINGS -Raw) -eq $userData) 'uninstall preserves reading settings and bookmarks'
    Check ((RegistrySnapshot 'Software\Classes\.pdf\OpenWithProgids') -eq $beforeOpenWith -and
        (RegistrySnapshot 'Software\RegisteredApplications') -eq $beforeRegistered -and
        (RegistrySnapshot 'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\UserChoice') -eq $beforeDefault) 'the real user PDF handlers, default choice and registered applications are unchanged'
    Write-Host "INSTALLER PASS ($script:Checks checks). Logs: $testRoot"
} finally {
    if (Test-Path -LiteralPath $uninstaller) {
        try { [void](Run $uninstaller @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') 'cleanup-uninstall.log') }
        catch { Write-Warning 'The isolated test uninstaller could not be run; inspect the test workspace.' }
    }
    # This literal key belongs exclusively to this test; real Classes keys are
    # never passed to a recursive delete.
    if (Test-Path -LiteralPath $testRegistry) { Remove-Item -LiteralPath $testRegistry -Recurse -Force }
}
