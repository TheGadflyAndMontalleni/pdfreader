[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Pdf,
    [string] $TranslationPlugin,
    [switch] $SkipBuild,
    [string[]] $Scales = @('1','1.5','2')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
if (-not (Test-Path -LiteralPath $Pdf)) { throw "Missing PDF fixture: $Pdf" }
$fixtures = Join-Path $root ('build/epub-check-' + [Guid]::NewGuid().ToString('N'))
& (Join-Path $root '.venv/Scripts/python.exe') (Join-Path $PSScriptRoot 'make_epub_fixtures.py') $fixtures
if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed.' }
$env:EPUB_TEST_FIXTURES = $fixtures
$env:EPUB_TEST_PDF = [IO.Path]::GetFullPath($Pdf)
$env:EPUB_TEST_PLUGIN = if ($TranslationPlugin) { [IO.Path]::GetFullPath($TranslationPlugin) } else { '' }
$env:QT_QPA_PLATFORM = 'windows'
foreach ($scale in $Scales) {
    $env:QT_SCALE_FACTOR = $scale
    $log = Join-Path $fixtures "results-$scale.txt"
    & (Join-Path $root 'build/epub_regressions.exe') -o "$log,txt"
    $code = $LASTEXITCODE
    Get-Content -LiteralPath $log
    if ($code -ne 0) { throw "EPUB checks failed at $scale x. Inspect $log" }
    $image = Join-Path $fixtures 'reader-preview.png'
    if (Test-Path -LiteralPath $image) { Copy-Item -LiteralPath $image -Destination (Join-Path $fixtures "reader-preview-$scale.png") -Force }
}
Write-Host "EPUB checks passed. Logs and real WebView2 screenshots: $fixtures"
