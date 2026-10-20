[CmdletBinding()]
param([switch] $SkipBuild, [switch] $SkipTests)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'build_plugin.ps1') -Source (Join-Path $root 'plugins/deepseek_translate') -BuildDir (Join-Path $root 'build/deepseek-translate')
    if ($LASTEXITCODE -ne 0) { throw 'Translation plugin build failed.' }
}
if (-not $SkipTests) {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'verify_translation_plugin.ps1') -SkipBuild
    if ($LASTEXITCODE -ne 0) { throw 'Translation plugin verification failed.' }
}
$version = (Get-Content -LiteralPath (Join-Path $root 'plugins/deepseek_translate/deepseek_translate.json') -Raw | ConvertFrom-Json).Version
$stage = Join-Path $root ('build/translation-package-' + [Guid]::NewGuid().ToString('N'))
$output = Join-Path $root 'dist/plugins'
New-Item -ItemType Directory -Force -Path $stage,$output,(Join-Path $stage 'licenses') | Out-Null
$pluginName = 'DeepSeekTranslate.dll'
Copy-Item -LiteralPath (Join-Path $root 'build/deepseek-translate/reader-plugins/DeepSeekTranslate.dll') -Destination (Join-Path $stage $pluginName) -Force
Copy-Item -LiteralPath (Join-Path $root 'plugins/deepseek_translate/README.md') -Destination (Join-Path $stage 'README.md') -Force
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage -Force
foreach ($license in @('LGPL-3.0-only.txt','GPL-3.0-only.txt')) {
    Copy-Item -LiteralPath (Join-Path $root "docs/licenses/$license") -Destination (Join-Path $stage 'licenses') -Force
}
Copy-Item -LiteralPath (Join-Path $root 'tools/sbom/qtbase-6.8.3.spdx') -Destination (Join-Path $stage 'licenses') -Force
Copy-Item -LiteralPath (Join-Path $stage $pluginName) -Destination (Join-Path $output $pluginName) -Force
$zipName = "DeepSeekTranslate-$version.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath (Join-Path $output $zipName) -Force
$hashes = foreach ($name in @($pluginName, $zipName)) {
    (Get-FileHash -LiteralPath (Join-Path $output $name)).Hash.ToLowerInvariant() + '  ' + $name
}
Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Value $hashes -Encoding utf8NoBOM
Get-ChildItem -LiteralPath $output -File | Select-Object Name,Length
