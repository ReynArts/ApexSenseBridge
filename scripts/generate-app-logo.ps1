param(
    [string]$Source,
    [string]$AssetsDirectory,
    [string]$PlayniteIcon
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $AssetsDirectory) { $AssetsDirectory = Join-Path $root "assets" }
if (-not $Source) { $Source = Join-Path $AssetsDirectory "logo-transparent.png" }
if (-not $PlayniteIcon) { $PlayniteIcon = Join-Path $root "playnite\ApexSenseBridge\icon.png" }
if (-not (Test-Path -LiteralPath $Source)) { throw "Logo not found: $Source" }

Add-Type -AssemblyName System.Drawing
$emblem = [System.Drawing.Image]::FromFile($Source)

function New-RoundedPath([double]$size, [double]$radius) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($size - $d, 0, $d, $d, 270, 90)
    $path.AddArc($size - $d, $size - $d, $d, $d, 0, 90)
    $path.AddArc(0, $size - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-PlateBitmap([int]$size, [double]$emblemShare) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $path = New-RoundedPath $size ($size * 0.2)
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Rectangle 0, 0, $size, $size), ([System.Drawing.Color]::FromArgb(255, 20, 20, 28)), ([System.Drawing.Color]::FromArgb(255, 10, 10, 14)), 90
    $graphics.FillPath($brush, $path)
    $side = [int][Math]::Round($size * $emblemShare)
    $offset = [int][Math]::Round(($size - $side) / 2)
    $graphics.DrawImage($emblem, (New-Object System.Drawing.Rectangle $offset, $offset, $side, $side))
    $brush.Dispose()
    $path.Dispose()
    $graphics.Dispose()
    return $bitmap
}

function Get-PngBytes([System.Drawing.Bitmap]$bitmap) {
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    return ,[byte[]]$stream.ToArray()
}

$plate = New-PlateBitmap 512 0.84
$plate.Save((Join-Path $AssetsDirectory "logo.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$plate.Dispose()

$playnite = New-PlateBitmap 256 0.84
$playnite.Save($PlayniteIcon, [System.Drawing.Imaging.ImageFormat]::Png)
$playnite.Dispose()

$sizes = 256, 128, 64, 48, 32, 24, 16
$images = @()
foreach ($size in $sizes) {
    $share = if ($size -le 32) { 0.94 } else { 0.86 }
    $bitmap = New-PlateBitmap $size $share
    $images += , @{ Size = $size; Bytes = (Get-PngBytes $bitmap) }
    $bitmap.Dispose()
}

$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $stream
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $dimension = if ($image.Size -ge 256) { 0 } else { $image.Size }
    $writer.Write([byte]$dimension)
    $writer.Write([byte]$dimension)
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$image.Bytes.Length)
    $writer.Write([uint32]$offset)
    $offset += $image.Bytes.Length
}
foreach ($image in $images) { $writer.Write([byte[]]$image.Bytes) }
$writer.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $AssetsDirectory "app.ico"), $stream.ToArray())

$emblem.Dispose()
Write-Host "Generated logo.png, app.ico and the Playnite icon from $Source"
