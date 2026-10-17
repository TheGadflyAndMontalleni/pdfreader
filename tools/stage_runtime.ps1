# Stage only files required by a distributed reader, including license texts.
# Emits the relative payload names for make_sfx.ps1 to embed.
[CmdletBinding()]
param([Parameter(Mandatory)][string] $Destination)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$target = [IO.Path]::GetFullPath($Destination)
$allowed = [IO.Path]::GetFullPath($build).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if (-not $target.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Staging directory must be a child of the project build directory: $target"
}
if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
New-Item -ItemType Directory -Force -Path $target | Out-Null
$files = @(
    'pdfreader.exe','pdfium.dll','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll',
    'Qt6PrintSupport.dll','Qt6Network.dll','libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll'
)
foreach ($name in $files) {
    $source = Join-Path $build $name
    if (-not (Test-Path -LiteralPath $source)) { throw "Required runtime missing: $source" }
    Copy-Item -LiteralPath $source -Destination $target -Force
}
foreach ($directory in 'platforms','styles','imageformats') {
    $source = Join-Path $build $directory
    if (-not (Test-Path -LiteralPath $source)) { continue }
    $output = Join-Path $target $directory
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    Get-ChildItem -LiteralPath $source -Filter '*.dll' | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $output -Force
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $target 'platforms/qwindows.dll'))) {
    throw 'The Windows Qt platform plugin is required.'
}
foreach ($name in 'LICENSE','THIRD_PARTY_NOTICES.md') {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $target -Force
}
$licenses = Join-Path $target 'licenses'
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
Copy-Item -Path (Join-Path $root 'docs/licenses/*.txt') -Destination $licenses -Force
Copy-Item -LiteralPath (Join-Path $root 'tools/sbom/qtbase-6.8.3.spdx') -Destination (Join-Path $licenses 'qtbase-6.8.3.spdx') -Force
Copy-Item -LiteralPath (Join-Path $root 'tools/pdfium/licenses') -Destination (Join-Path $licenses 'pdfium') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'tools/pdfium/LICENSE') -Destination (Join-Path $licenses 'pdfium-binaries-LICENSE.txt') -Force
$mingw = Join-Path $root 'tools/Tools/mingw1310_64/licenses'
foreach ($component in 'gcc','mingw-w64','winpthreads') {
    $source = Join-Path $mingw $component
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing runtime license directory: $source" }
    New-Item -ItemType Directory -Force -Path (Join-Path $licenses 'mingw') | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $licenses "mingw/$component") -Recurse -Force
}
Get-ChildItem -LiteralPath $target -Recurse -File | ForEach-Object {
    [IO.Path]::GetRelativePath($target, $_.FullName)
}
