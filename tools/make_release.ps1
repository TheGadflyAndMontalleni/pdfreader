[CmdletBinding()]
param([switch] $SkipBuild, [string] $InnoCompiler)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
& pwsh -NoProfile -File (Join-Path $PSScriptRoot 'make_sfx.ps1') -SkipBuild -OutputName 'PDFReader-Portable.exe'
if ($LASTEXITCODE -ne 0) { throw 'Portable packaging failed.' }
$installerArgs = @('-NoProfile','-File',(Join-Path $PSScriptRoot 'make_installer.ps1'),'-SkipBuild')
if ($InnoCompiler) { $installerArgs += @('-InnoCompiler',$InnoCompiler) }
& pwsh @installerArgs
if ($LASTEXITCODE -ne 0) { throw 'Installer packaging failed.' }
$dist = Join-Path $root 'dist'
$sdkStage = Join-Path $root 'build/plugin-sdk'
$sdkStageAbsolute = [IO.Path]::GetFullPath($sdkStage)
$buildRoot = [IO.Path]::GetFullPath((Join-Path $root 'build')).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if (-not $sdkStageAbsolute.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'SDK staging must stay inside build/.' }
if (Test-Path -LiteralPath $sdkStageAbsolute) { Remove-Item -LiteralPath $sdkStageAbsolute -Recurse -Force }
New-Item -ItemType Directory -Force -Path $sdkStage,(Join-Path $sdkStage 'plugins'),(Join-Path $sdkStage 'docs'),(Join-Path $sdkStage 'tools') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'sdk') -Destination $sdkStage -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'plugins/reading_tools') -Destination (Join-Path $sdkStage 'plugins') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'docs/plugin-development.md') -Destination (Join-Path $sdkStage 'docs') -Force
Copy-Item -LiteralPath (Join-Path $root 'tools/build_plugin.ps1') -Destination (Join-Path $sdkStage 'tools') -Force
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $sdkStage -Force
Set-Content -LiteralPath (Join-Path $sdkStage 'README.md') -Encoding utf8NoBOM -Value '# PDF Reader Plugin SDK 1.1 / API 1', '', '开发指南：docs/plugin-development.md。新增可选选区坐标能力，HostV1 / PluginV1 二进制接口保持不变。示例插件：plugins/reading_tools/。使用与阅读器一致的 Qt 6.8.3、MinGW 13.1.0、x64、Release 构建。'
Compress-Archive -Path (Join-Path $sdkStage '*') -DestinationPath (Join-Path $dist 'PDFReader-PluginSDK-1.1.zip') -Force
# Preserve the established download URL; the API 1 interface is unchanged.
Copy-Item -LiteralPath (Join-Path $dist 'PDFReader-PluginSDK-1.1.zip') -Destination (Join-Path $dist 'PDFReader-PluginSDK-1.0.zip') -Force
$names = @('PDFReader-Setup.exe','PDFReader-Portable.exe','PDFReader-PluginSDK-1.1.zip','PDFReader-PluginSDK-1.0.zip')
$checksums = foreach ($name in $names) {
    (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $name
}
Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Value $checksums -Encoding utf8NoBOM
Write-Host "Release files: $dist"
foreach ($name in ($names + 'SHA256SUMS.txt')) { Get-Item -LiteralPath (Join-Path $dist $name) | Select-Object Name,Length }
