#!/usr/bin/env pwsh
# Reproducible verification for pdfreader.
#
# Runs every claim the reader makes and prints a PASS/FAIL summary. Exit code is
# 0 only when all of them pass, so this doubles as a smoke test.
#
#   pwsh -NoProfile -File tools/verify.ps1
#   pwsh -NoProfile -File tools/verify.ps1 -Corpus D:\path\to\pdfs
#
# Everything runs against a throwaway settings file, so your real reading
# positions and recent-file list are never touched.

[CmdletBinding()]
param(
    # Directory searched recursively for PDFs (the table-of-contents survey).
    [string] $Corpus = 'D:\harness_test',
    # Where the built binaries live.
    [string] $BuildDir = "$PSScriptRoot\..\build",
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$OutputEncoding = [Text.Encoding]::UTF8

$exe   = Join-Path $BuildDir 'pdfreader.exe'
$tocExe = Join-Path $BuildDir 'toc_build.exe'
$tmp   = Join-Path ([IO.Path]::GetTempPath()) ("pdfreader-verify-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# Keep the user's real configuration out of the way for the whole run.
$env:PDFREADER_SETTINGS = Join-Path $tmp 'settings.ini'

$script:Results = @()
function Record([string]$Name, [bool]$Ok, [string]$Detail = '') {
    $script:Results += [pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail }
    $tag = if ($Ok) { 'PASS' } else { 'FAIL' }
    Write-Host ("  [{0}] {1}" -f $tag, $Name) -ForegroundColor $(if ($Ok) { 'Green' } else { 'Red' })
    if ($Detail) { Write-Host "         $Detail" -ForegroundColor DarkGray }
}

function Invoke-Reader {
    param([string[]] $Arguments)
    return Invoke-Executable -Program $exe -Arguments $Arguments
}

function Invoke-Executable {
    param([string] $Program, [string[]] $Arguments)
    $out = Join-Path $tmp 'out.txt'
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Program
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $result = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    Set-Content -LiteralPath $out -Value $result -Encoding utf8
    $code = $process.ExitCode
    $process.Dispose()
    return @{ Code = $code; Text = $result }
}

if (-not $SkipBuild) {
    Write-Host "`n=== build ===" -ForegroundColor Cyan
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'build.ps1') 2>&1 |
        Select-String -Pattern 'error|Error' | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
}
if (-not (Test-Path $exe)) { throw "missing $exe (run tools/build.ps1)" }
# Exercise real widgets without flashing test windows on the user's desktop.
$pluginRoot = Join-Path $PSScriptRoot '..\tools\plugins'
if (-not $env:QT_QPA_PLATFORM -and (Test-Path (Join-Path $pluginRoot 'platforms\qoffscreen.dll'))) {
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_PLUGIN_PATH = $pluginRoot
}

# The self-test needs one document with text, a printed table of contents and
# enough pages to scroll. Any of the corpus files works.
$sample = Get-ChildItem $Corpus -Filter 'quality2020.pdf' -Recurse -ErrorAction SilentlyContinue |
          Select-Object -First 1
if (-not $sample) {
    $sample = Get-ChildItem $Corpus -Filter '*.pdf' -Recurse -ErrorAction SilentlyContinue |
              Select-Object -First 1
}
if (-not $sample) { throw "no PDF found under $Corpus" }

Write-Host "`n=== 1. reader self-test ($($sample.Name)) ===" -ForegroundColor Cyan
$r = Invoke-Reader @('--selftest', $sample.FullName)
$pass = ([regex]::Matches($r.Text, '(?m)^PASS')).Count
$fail = ([regex]::Matches($r.Text, '(?m)^FAIL')).Count
$info = ([regex]::Matches($r.Text, '(?m)^INFO')).Count
Record 'reader self-test' ($r.Code -eq 0 -and $fail -eq 0) "$pass assertions passed, $fail failed"
foreach ($line in ($r.Text -split "`n" | Where-Object { $_ -match '^INFO' })) {
    Write-Host "         $($line.Trim())" -ForegroundColor DarkGray
}
if ($fail -gt 0) {
    ($r.Text -split "`n" | Where-Object { $_ -match '^FAIL' }) |
        ForEach-Object { Write-Host "         $($_.Trim())" -ForegroundColor Red }
}

Write-Host "`n=== 2. tabs and per-tab state ===" -ForegroundColor Cyan
$two = Get-ChildItem $Corpus -Filter '*.pdf' -Recurse -ErrorAction SilentlyContinue |
       Where-Object { $_.Name -ne $sample.Name } | Select-Object -First 1
if ($two) {
    $r = Invoke-Reader @('--selftest-tabs', "$($sample.FullName);$($two.FullName)")
    $p2 = ([regex]::Matches($r.Text, '(?m)^PASS')).Count
    $f2 = ([regex]::Matches($r.Text, '(?m)^FAIL')).Count
    Record 'tabs + view state' ($r.Code -eq 0 -and $f2 -eq 0) "$p2 passed, $f2 failed"
} else {
    Record 'tabs + view state' $false 'need two PDFs in the corpus'
}

Write-Host "`n=== 3. cross-process session restore ===" -ForegroundColor Cyan
$null = Invoke-Reader @('--session-write', $sample.FullName, '--page', '12', '--zoom', '1.25')
$r = Invoke-Reader @('--session-read', $sample.FullName, '--page', '12', '--zoom', '1.25')
$observed = ($r.Text -split "`n" | Where-Object { $_ -match 'observed:' }) -join ''
Record 'session restore' ($r.Code -eq 0 -and $r.Text -match 'SESSION PASS') $observed.Trim()
# Negative control: asking for a position that was not stored must fail.
$r = Invoke-Reader @('--session-read', $sample.FullName, '--page', '5', '--zoom', '1.25')
Record 'session restore rejects a wrong expectation' ($r.Code -ne 0) 'negative control'

Write-Host "`n=== 4. vector export (print path) ===" -ForegroundColor Cyan
$pdf = Join-Path $tmp 'export.pdf'
Remove-Item $pdf -Force -ErrorAction SilentlyContinue
$r = Invoke-Reader @('--print-test', $sample.FullName, '--print-out', $pdf,
                     '--page', '2', '--page-to', '6')
$detail = ($r.Text -split "`n" | Where-Object { $_ -match 'exported' }) -join ''
Record 'export 5 pages with a text layer' ($r.Code -eq 0 -and $r.Text -match 'PRINT PASS') $detail.Trim()
Record 'export produced a file' (Test-Path $pdf) $(if (Test-Path $pdf) { "{0:N0} KB" -f ((Get-Item $pdf).Length / 1KB) })
$r = Invoke-Reader @('--print-test', $sample.FullName, '--print-out', 'Z:\nope\x.pdf',
                     '--page', '2', '--page-to', '6')
Record 'export to an unwritable path fails' ($r.Code -ne 0) 'negative control'

Write-Host "`n=== 5. table of contents over the corpus ===" -ForegroundColor Cyan
if (Test-Path $tocExe) {
    $loadable = 0; $covered = 0; $nodes = 0; $unresolved = 0
    $bySource = @{}
    foreach ($f in (Get-ChildItem $Corpus -Filter '*.pdf' -Recurse -ErrorAction SilentlyContinue)) {
        $out = Join-Path $tmp 'toc.txt'
        & $tocExe $f.FullName 2>&1 | Out-File -Encoding utf8 $out
        $raw = Get-Content $out -Raw
        if ($raw -match 'FAIL open') { continue }   # not a PDF we can read
        $loadable++
        $src = if ($raw -match '(?m)^source\s+:\s*(\S+)') { $matches[1] } else { '?' }
        $n   = if ($raw -match 'nodes\s+:\s*(\d+)') { [int]$matches[1] } else { 0 }
        $u   = if ($raw -match 'unresolved=(\d+)') { [int]$matches[1] } else { 0 }
        $nodes += $n; $unresolved += $u
        if ($n -gt 0) { $covered++ }
        $bySource[$src] = 1 + ($bySource[$src] | ForEach-Object { $_ })
    }
    foreach ($k in ($bySource.Keys | Sort-Object)) {
        Write-Host ("         {0,-20} {1}" -f $k, $bySource[$k]) -ForegroundColor DarkGray
    }
    Record 'every loadable PDF has a table of contents' ($loadable -gt 0 -and $covered -eq $loadable) `
           "$covered / $loadable documents, $nodes entries"
    Record 'no table-of-contents entry has an unresolved target' ($unresolved -eq 0) `
           "$unresolved unresolved"
} else {
    Record 'table-of-contents survey' $false "missing $tocExe"
}

Write-Host "`n=== 6. packaging artefacts ===" -ForegroundColor Cyan
# The icon and the version block live in the PE resource table. Losing either is
# silent: the app still runs, it just looks like an anonymous executable again.
Add-Type -AssemblyName System.Drawing
$sfxPath = Join-Path $PSScriptRoot '..\dist\PDFReader-Portable.exe'
if (-not (Test-Path $sfxPath)) { $sfxPath = Join-Path $PSScriptRoot '..\dist\PDFReader.exe' }
foreach ($candidate in @(
        @{ Name = 'reader exe'; Path = $exe }
        @{ Name = 'single-file package'; Path = $sfxPath }
    )) {
    if (-not (Test-Path $candidate.Path)) {
        Record "$($candidate.Name) built" $false "missing $($candidate.Path)"
        continue
    }
    $icon = [System.Drawing.Icon]::ExtractAssociatedIcon($candidate.Path)
    $info = (Get-Item $candidate.Path).VersionInfo
    Record "$($candidate.Name) has an icon" ($null -ne $icon) `
           ("{0:N1} MB" -f ((Get-Item $candidate.Path).Length / 1MB))
    $hasVersion = -not [string]::IsNullOrWhiteSpace($info.ProductName) -and
                  -not [string]::IsNullOrWhiteSpace($info.FileVersion)
    Record "$($candidate.Name) has version info" $hasVersion `
           "$(if ($info.ProductName) { $info.ProductName } else { '(blank)' }) $($info.FileVersion)"
    if ($icon) { $icon.Dispose() }
}

$iconSource = Join-Path $PSScriptRoot '..\src\app\app.ico'
if (Test-Path $iconSource) {
    # Windows asks for a different size per surface (16 in menus, 256 in the
    # large-icon view), so a single-size icon would look blurry somewhere.
    $iconBytes = [System.IO.File]::ReadAllBytes($iconSource)
    $iconCount = [BitConverter]::ToUInt16($iconBytes, 4)
    Record 'icon file carries multiple sizes' ($iconCount -ge 4) "$iconCount sizes"
} else {
    Record 'icon file exists' $false 'run tools/make_icon.ps1'
}

Write-Host "`n=== 7. reading, bookmarks and high-DPI regressions ===" -ForegroundColor Cyan
$regressions = Join-Path $BuildDir 'reader_regressions.exe'
if ((Test-Path $regressions) -and $two) {
    $previousScale = $env:QT_SCALE_FACTOR
    $previousPlatform = $env:QT_QPA_PLATFORM
    $previousPlugins = $env:QT_PLUGIN_PATH
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QT_PLUGIN_PATH = $pluginRoot
        foreach ($scale in @('1', '1.5', '2')) {
            $env:QT_SCALE_FACTOR = $scale
            $r = Invoke-Executable -Program $regressions -Arguments @($sample.FullName, $two.FullName)
            $assertions = ([regex]::Matches($r.Text, '(?m)^PASS')).Count
            Record "reading regressions at ${scale}x" ($r.Code -eq 0 -and $r.Text -match 'REGRESSIONS PASS') "$assertions assertions"
            foreach ($line in ($r.Text -split "`n" | Where-Object { $_ -match '^INFO|^FAIL' })) {
                Write-Host "         $($line.Trim())"
            }
        }
    } finally {
        $env:QT_SCALE_FACTOR = $previousScale
        $env:QT_QPA_PLATFORM = $previousPlatform
        $env:QT_PLUGIN_PATH = $previousPlugins
    }
} else {
    Record 'reading regression executable and two fixtures available' $false 'rebuild the project and provide two PDFs'
}

Write-Host "`n=== 8. native plugin lifecycle ===" -ForegroundColor Cyan
$pluginTests = Join-Path $BuildDir 'plugin_regressions.exe'
if ((Test-Path -LiteralPath $pluginTests) -and $two) {
    $r = Invoke-Executable -Program $pluginTests -Arguments @($sample.FullName, $two.FullName)
    $checks = ([regex]::Matches($r.Text, '(?m)^PASS')).Count
    Record 'plugin SDK, lifecycle and management' ($r.Code -eq 0 -and $r.Text -match 'PLUGIN REGRESSIONS PASS') "$checks assertions"
    foreach ($line in ($r.Text -split "`n" | Where-Object { $_ -match '^FAIL' })) { Write-Host "         $line" }
} else { Record 'plugin regression executable and fixtures available' $false 'rebuild the project' }
$r = Invoke-Reader @('--selftest-plugins', $sample.FullName)
Record 'bundled plugin in the distributed reader' ($r.Code -eq 0 -and $r.Text -match 'PLUGIN SELFTEST PASS') $r.Text.Trim()

Write-Host "`n=== summary ===" -ForegroundColor Cyan
$failed = @($script:Results | Where-Object { -not $_.Ok })
foreach ($res in $script:Results) {
    $tag = if ($res.Ok) { 'PASS' } else { 'FAIL' }
    $color = if ($res.Ok) { 'Green' } else { 'Red' }
    Write-Host ("  {0}  {1}" -f $tag, $res.Name) -ForegroundColor $color
}
Write-Host ""
if ($failed.Count -eq 0) {
    Write-Host "ALL CHECKS PASSED ($($script:Results.Count))" -ForegroundColor Green
    exit 0
}
Write-Host "$($failed.Count) of $($script:Results.Count) CHECKS FAILED" -ForegroundColor Red
exit 1
