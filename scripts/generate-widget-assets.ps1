param(
    [string]$Logo,
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Logo) { $Logo = Join-Path $root "assets\logo-transparent.png" }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root "ApexSenseBridgeWidget\Assets" }
if (-not (Test-Path -LiteralPath $Logo)) { throw "Logo not found: $Logo" }

Add-Type -AssemblyName System.Drawing
$source = [System.Drawing.Image]::FromFile($Logo)
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
Get-ChildItem -LiteralPath $OutputDirectory -Filter "*.png" | Remove-Item -Force

function New-Canvas([int]$width, [int]$height, [System.Drawing.Color]$background) {
    $bitmap = New-Object System.Drawing.Bitmap $width, $height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear($background)
    return @{ Bitmap = $bitmap; Graphics = $graphics }
}

function Save-Logo([string]$name, [int]$width, [int]$height, [double]$logoShare, [System.Drawing.Color]$background) {
    $canvas = New-Canvas $width $height $background
    $side = [int][Math]::Round([Math]::Min($width, $height) * $logoShare)
    $left = [int][Math]::Round(($width - $side) / 2)
    $top = [int][Math]::Round(($height - $side) / 2)
    $canvas.Graphics.DrawImage($source, (New-Object System.Drawing.Rectangle $left, $top, $side, $side))
    $canvas.Bitmap.Save((Join-Path $OutputDirectory $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $canvas.Graphics.Dispose()
    $canvas.Bitmap.Dispose()
}

$transparent = [System.Drawing.Color]::Transparent
$splashBackground = [System.Drawing.Color]::FromArgb(255, 10, 10, 14)

foreach ($scale in 100, 200, 400) {
    $factor = $scale / 100
    Save-Logo "Square44x44Logo.scale-$scale.png" ([int](44 * $factor)) ([int](44 * $factor)) 1.0 $transparent
    if ($scale -le 200) { Save-Logo "Square150x150Logo.scale-$scale.png" ([int](150 * $factor)) ([int](150 * $factor)) 1.0 $transparent }
    Save-Logo "SmallTile.scale-$scale.png" ([int](71 * $factor)) ([int](71 * $factor)) 1.0 $transparent
    Save-Logo "StoreLogo.scale-$scale.png" ([int](50 * $factor)) ([int](50 * $factor)) 1.0 $transparent
    if ($scale -le 200) {
        Save-Logo "Wide310x150Logo.scale-$scale.png" ([int](310 * $factor)) ([int](150 * $factor)) 1.0 $transparent
        if ($scale -eq 100) { Save-Logo "LargeTile.scale-$scale.png" ([int](310 * $factor)) ([int](310 * $factor)) 1.0 $transparent }
        Save-Logo "SplashScreen.scale-$scale.png" ([int](620 * $factor)) ([int](300 * $factor)) 0.62 $splashBackground
    }
}
foreach ($size in 16, 24, 32, 48, 256) {
    Save-Logo "Square44x44Logo.targetsize-$size.png" $size $size 1.0 $transparent
    Save-Logo "Square44x44Logo.targetsize-${size}_altform-unplated.png" $size $size 1.0 $transparent
}

$source.Dispose()
Write-Host ("Generated {0} images in {1}" -f (Get-ChildItem -LiteralPath $OutputDirectory -Filter "*.png").Count, $OutputDirectory)
