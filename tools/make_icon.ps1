# Build a multi-resolution .ico from a single PNG.
#
#   pwsh -NoProfile -File tools/make_icon.ps1
#   pwsh -NoProfile -File tools/make_icon.ps1 -Source "C:\path\icon.png"
#
# Windows picks a different icon size per surface (16 in the title bar and
# menus, 32/48 in Explorer, 256 in the large-icon view), so an .ico with only
# one size looks blurry wherever Windows has to scale it.
#
# Entries are stored as embedded PNGs, which Vista and later read directly. That
# keeps the file small and avoids the BMP/AND-mask quirks of the old format.
[CmdletBinding()]
param(
    [string] $Source = 'D:\Users\RimeSun\Downloads\[CITYPNG.COM]PDF File Document Icon PNG - 800x800.png',
    [string] $Out    = "$PSScriptRoot\..\src\app\app.ico",
    # Also emitted as a PNG for the Qt window icon (see app.qrc).
    [string] $PngOut = "$PSScriptRoot\..\src\app\icon.png",
    # Sizes Windows actually asks for. 256 must be last: its directory entry
    # stores 0 for width/height, meaning "256".
    [int[]]  $Sizes  = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path -LiteralPath $Source)) { throw "missing source image: $Source" }

$src = [System.Drawing.Image]::FromFile((Resolve-Path -LiteralPath $Source).Path)
Write-Host "source: $($src.Width)x$($src.Height)  ->  $Out" -ForegroundColor Cyan

# Render one size to PNG bytes.
function Get-PngBytes([System.Drawing.Image] $image, [int] $size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size,
                                            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($image, (New-Object System.Drawing.Rectangle(0, 0, $size, $size)))
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $bytes = $ms.ToArray()
    $ms.Dispose()
    return ,$bytes
}

$images = @()
$pngIndex = [Array]::IndexOf($Sizes, 256)
foreach ($size in $Sizes) { $images += ,(Get-PngBytes $src $size) }

# The Qt window icon wants a single PNG; reuse the 256 render.
if ($PngOut -and $pngIndex -ge 0) {
    [System.IO.File]::WriteAllBytes($PngOut, $images[$pngIndex])
    Write-Host "wrote $PngOut  ($($images[$pngIndex].Length) bytes, 256x256)" -ForegroundColor Green
}

$src.Dispose()

# ICONDIR (6) + ICONDIRENTRY per image (16) + the PNG payloads.
$outDir = Split-Path -Parent $Out
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$fs = [System.IO.File]::Create($Out)
$bw = New-Object System.IO.BinaryWriter($fs)

$bw.Write([UInt16]0)              # reserved
$bw.Write([UInt16]1)              # 1 = icon
$bw.Write([UInt16]$Sizes.Count)

$offset = 6 + 16 * $Sizes.Count
for ($i = 0; $i -lt $Sizes.Count; $i++) {
    $size = $Sizes[$i]
    $bw.Write([Byte]$(if ($size -ge 256) { 0 } else { $size }))   # width
    $bw.Write([Byte]$(if ($size -ge 256) { 0 } else { $size }))   # height
    $bw.Write([Byte]0)            # palette size
    $bw.Write([Byte]0)            # reserved
    $bw.Write([UInt16]1)          # colour planes
    $bw.Write([UInt16]32)         # bits per pixel
    $bw.Write([UInt32]$images[$i].Length)
    $bw.Write([UInt32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $bw.Write($img) }

$bw.Flush()
$bw.Dispose()
$fs.Dispose()

$info = Get-Item $Out
Write-Host ("wrote {0}  ({1:N0} bytes, {2} sizes: {3})" -f `
    $Out, $info.Length, $Sizes.Count, ($Sizes -join ', ')) -ForegroundColor Green

# Sanity check: read the directory back and confirm the declared sizes.
$verify = [System.IO.File]::ReadAllBytes($Out)
$count = [BitConverter]::ToUInt16($verify, 4)
$read = @()
for ($i = 0; $i -lt $count; $i++) {
    $base = 6 + 16 * $i
    $w = if ($verify[$base] -eq 0) { 256 } else { [int]$verify[$base] }
    $read += $w
}
Write-Host "verified sizes in the ico: $($read -join ', ')" -ForegroundColor DarkGray
