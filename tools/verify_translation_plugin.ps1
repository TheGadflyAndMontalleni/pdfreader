[CmdletBinding()]
param([switch] $SkipBuild, [switch] $ProbeNetwork, [string[]] $ReaderExe = @())
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pluginBuild = Join-Path $root 'build/deepseek-translate'
if (-not $SkipBuild) {
    if (-not (Test-Path -LiteralPath (Join-Path $pluginBuild 'data/ecdict.pdict'))) {
        & (Join-Path $root '.venv/Scripts/python.exe') (Join-Path $PSScriptRoot 'prepare_translation_dictionary.py') --output (Join-Path $pluginBuild 'data')
        if ($LASTEXITCODE -ne 0) { throw 'Dictionary preparation failed.' }
    }
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'build_plugin.ps1') -Source (Join-Path $root 'plugins/deepseek_translate') -BuildDir $pluginBuild
    if ($LASTEXITCODE -ne 0) { throw 'Translation plugin build failed.' }
}
$env:Path = (Join-Path $root 'tools/bin') + ';' + (Join-Path $root 'tools/Tools/mingw1310_64/bin') + ';' + $env:Path
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_PLUGIN_PATH = Join-Path $root 'tools/plugins'
$env:PDFREADER_TEST_ARTIFACTS = Join-Path $root 'build/audit/deepseek-ui'
$log = Join-Path $pluginBuild 'translation-tests.log'
& (Join-Path $pluginBuild 'deepseek_translation_tests.exe') -o "$log,txt"
$testExit = $LASTEXITCODE
Get-Content -LiteralPath $log | Where-Object { $_ -match '^PASS|^FAIL|^Totals' }
if ($testExit -ne 0) { throw "Translation tests failed. See $log" }
if ($ProbeNetwork) {
    & (Join-Path $pluginBuild 'deepseek_translation_tests.exe') --probe-deepseek
    if ($LASTEXITCODE -ne 0) { throw 'Official HTTPS probe failed; check system network/proxy.' }
}
foreach ($reader in $ReaderExe) {
    $reader = [IO.Path]::GetFullPath($reader)
    if (-not (Test-Path -LiteralPath $reader)) { throw "Reader not found: $reader" }
    $testRoot = Join-Path $root ('build/deepseek-reader-check-' + [Guid]::NewGuid().ToString('N'))
    $pluginDir = Join-Path $testRoot 'plugins'
    $platformRoot = Join-Path $testRoot 'qt-platform'
    New-Item -ItemType Directory -Force -Path $pluginDir,(Join-Path $platformRoot 'platforms') | Out-Null
    Copy-Item -LiteralPath (Join-Path $pluginBuild 'reader-plugins/DeepSeekTranslate.dll') -Destination $pluginDir
    Copy-Item -LiteralPath (Join-Path $root 'tools/plugins/platforms/qoffscreen.dll') -Destination (Join-Path $platformRoot 'platforms')
    $settingsPath = Join-Path $testRoot 'settings.ini'
    Set-Content -LiteralPath $settingsPath -Encoding utf8NoBOM -Value '[plugins]','enabled=org.pdfreader.deepseek-translate'
    $processInfo = [Diagnostics.ProcessStartInfo]::new()
    $processInfo.FileName = $reader
    $processInfo.UseShellExecute = $false
    $processInfo.CreateNoWindow = $true
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    $processInfo.Environment['PDFREADER_SETTINGS'] = $settingsPath
    $processInfo.Environment['QT_PLUGIN_PATH'] = $platformRoot
    [void]$processInfo.Environment.Remove('DEEPSEEK_API_KEY')
    # Open and print a synthetic page through the published reader while the
    # plugin initializes. The reader's general self-test needs its own corpus.
    $processInfo.ArgumentList.Add('--print-test')
    $fixture = Join-Path $pluginBuild 'synthetic-translation-fixture.pdf'
    if (-not (Test-Path -LiteralPath $fixture)) { throw 'The synthetic PDF fixture is missing; run translation tests first.' }
    $processInfo.ArgumentList.Add($fixture)
    $printedPdf = Join-Path $testRoot 'printed.pdf'
    $processInfo.ArgumentList.Add('--print-out')
    $processInfo.ArgumentList.Add($printedPdf)
    $processInfo.ArgumentList.Add('--page')
    $processInfo.ArgumentList.Add('1')
    $processInfo.ArgumentList.Add('--page-to')
    $processInfo.ArgumentList.Add('1')
    $process = [Diagnostics.Process]::Start($processInfo)
    $outputTask = $process.StandardOutput.ReadToEndAsync()
    $errorTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $text = $outputTask.Result + $errorTask.Result
    Set-Content -LiteralPath (Join-Path $testRoot 'reader.log') -Value $text
    if ($process.ExitCode -ne 0 -or $text -notmatch 'PRINT PASS' -or
        -not (Test-Path -LiteralPath $printedPdf) -or (Get-Item -LiteralPath $printedPdf).Length -eq 0) {
        throw "Reader integration failed. See $testRoot/reader.log"
    }
    $backend = @(Get-ChildItem -LiteralPath (Join-Path $testRoot 'plugin-data') -Recurse -Filter qschannelbackend.dll)
    if ($backend.Count -ne 1 -or (Get-FileHash -LiteralPath $backend[0].FullName).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $root 'tools/plugins/tls/qschannelbackend.dll')).Hash -or
        (Get-Content -LiteralPath $settingsPath -Raw) -notmatch 'enabled=org.pdfreader.deepseek-translate') {
        throw 'The reader did not initialize the single-file translation plugin successfully.'
    }
    Write-Host "Reader $((Get-Item -LiteralPath $reader).VersionInfo.ProductVersion): single-DLL loading and normal PDF reading passed."
}
