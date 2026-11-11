param(
    [string]$Reference,
    [string]$Output,
    [int]$Size = 1024,
    [string]$Background,
    [ValidateSet("Left", "Right")][string]$DualSenseSide = "Right",
    [double]$Margin = 0.05,
    [switch]$NoRim
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Reference) { $Reference = Join-Path $root "assets\controllers-reference.png" }
if (-not $Output) { $Output = Join-Path $root "assets\logo-transparent.png" }

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class LogoClean {
    public static void KeepLargest(Bitmap bmp, int threshold) {
        int w = bmp.Width, h = bmp.Height;
        Rectangle rect = new Rectangle(0, 0, w, h);
        BitmapData data = bmp.LockBits(rect, ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
        int stride = data.Stride;
        byte[] px = new byte[stride * h];
        Marshal.Copy(data.Scan0, px, 0, px.Length);
        int[] label = new int[w * h];
        List<int> sizes = new List<int>();
        sizes.Add(0);
        Stack<int> stack = new Stack<int>();
        for (int i = 0; i < w * h; i++) {
            int x0 = i % w, y0 = i / w;
            if (label[i] != 0 || px[y0 * stride + x0 * 4 + 3] <= threshold) continue;
            int id = sizes.Count;
            int count = 0;
            label[i] = id;
            stack.Push(i);
            while (stack.Count > 0) {
                int p = stack.Pop();
                count++;
                int px0 = p % w, py0 = p / w;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = px0 + dx, ny = py0 + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        int n = ny * w + nx;
                        if (label[n] != 0 || px[ny * stride + nx * 4 + 3] <= threshold) continue;
                        label[n] = id;
                        stack.Push(n);
                    }
                }
            }
            sizes.Add(count);
        }
        int best = 0, bestSize = 0;
        for (int k = 1; k < sizes.Count; k++) if (sizes[k] > bestSize) { bestSize = sizes[k]; best = k; }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (label[y * w + x] != best) {
                    int o = y * stride + x * 4;
                    px[o] = 0; px[o + 1] = 0; px[o + 2] = 0; px[o + 3] = 0;
                }
            }
        }
        Marshal.Copy(px, 0, data.Scan0, px.Length);
        bmp.UnlockBits(data);
    }
}
"@

$apexAxis = 362
$dsAxis = 1059
$dockMouth = 184
$mirror = 2 * $apexAxis

function New-Argb([int]$w, [int]$h) {
    $b = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    return $b
}

function Get-Graphics($bmp) {
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    return $g
}

function Repair-RowSpan($bmp, [int]$x0, [int]$x1, [int]$y0, [int]$y1, [int]$xa, [int]$xb) {
    for ($y = $y0; $y -le $y1; $y++) {
        $ca = @(0, 0, 0)
        $cb = @(0, 0, 0)
        $n = 0
        for ($dy = -1; $dy -le 1; $dy++) {
            $pa = $bmp.GetPixel($xa, $y + $dy)
            $pb = $bmp.GetPixel($xb, $y + $dy)
            $ca[0] += $pa.R; $ca[1] += $pa.G; $ca[2] += $pa.B
            $cb[0] += $pb.R; $cb[1] += $pb.G; $cb[2] += $pb.B
            $n++
        }
        for ($x = $x0; $x -le $x1; $x++) {
            $t = ($x - $xa) / [double]($xb - $xa)
            $r = [int](($ca[0] * (1 - $t) + $cb[0] * $t) / $n)
            $gg = [int](($ca[1] * (1 - $t) + $cb[1] * $t) / $n)
            $bb = [int](($ca[2] * (1 - $t) + $cb[2] * $t) / $n)
            $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, $r, $gg, $bb))
        }
    }
}

function Get-AlphaBounds($bmp, [int]$threshold = 40) {
    $w = $bmp.Width; $h = $bmp.Height
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    $minx = $w; $miny = $h; $maxx = -1; $maxy = -1
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $data.Stride
        for ($x = 0; $x -lt $w; $x++) {
            if ($bytes[$row + $x * 4 + 3] -gt $threshold) {
                if ($x -lt $minx) { $minx = $x }
                if ($x -gt $maxx) { $maxx = $x }
                if ($y -lt $miny) { $miny = $y }
                if ($y -gt $maxy) { $maxy = $y }
            }
        }
    }
    return @{ X0 = $minx; Y0 = $miny; X1 = $maxx; Y1 = $maxy }
}

$source = [System.Drawing.Bitmap]::FromFile($Reference)
$work = New-Argb $source.Width $source.Height
$gw = Get-Graphics $work
$gw.DrawImage($source, 0, 0, $source.Width, $source.Height)
$gw.Dispose()
$source.Dispose()

Repair-RowSpan $work 320 404 344 414 312 408
Repair-RowSpan $work 1022 1097 536 592 1018 1100
Repair-RowSpan $work 1051 1067 506 523 1050 1068

$edge = [System.Drawing.Color]::FromArgb(255, 27, 30, 40)
$cutLeft = @(@($dockMouth, 681), @(190, 675.5), @(200, 662), @(210, 651.5), @(220, 640), @(230, 633), @(240, 628), @(250, 625), @(262, 624), @(280, 623.5), @(300, 623), @($apexAxis, 622.5))
$strokeLeft = @(@(240, 625.5), @(250, 623), @(262, 622), @(280, 621.5), @(300, 621), @($apexAxis, 620.5))

function Expand-Mirror($pts) {
    $list = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    foreach ($p in $pts) { $list.Add((New-Object System.Drawing.PointF ([single]$p[0]), ([single]$p[1]))) }
    for ($i = $pts.Count - 2; $i -ge 0; $i--) {
        $p = $pts[$i]
        $list.Add((New-Object System.Drawing.PointF ([single]($mirror - $p[0])), ([single]$p[1])))
    }
    return ,$list
}
$cut = Expand-Mirror $cutLeft
$stroke = Expand-Mirror $strokeLeft

$gw = Get-Graphics $work
$gw.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
$clear = New-Object System.Collections.Generic.List[System.Drawing.PointF]
foreach ($p in $cut) { $clear.Add($p) }
$clear.Add((New-Object System.Drawing.PointF ([single]($mirror - $dockMouth + 1), 900)))
$clear.Add((New-Object System.Drawing.PointF ([single]($dockMouth - 1), 900)))
$gw.FillPolygon((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(0, 0, 0, 0))), $clear.ToArray())
$gw.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
$pen = New-Object System.Drawing.Pen $edge, 6
$pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
$pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$gw.DrawCurve($pen, $stroke.ToArray(), 0.5)
$gw.Dispose()

$apexRegion = if ($DualSenseSide -eq "Right") { New-Object System.Drawing.Rectangle 0, 0, ($apexAxis + 1), $work.Height } else { New-Object System.Drawing.Rectangle $apexAxis, 0, ($work.Width - $apexAxis), $work.Height }
$apexRegion.Width = [math]::Min($apexRegion.Width, 700 - $apexRegion.X)
$dsRegion = if ($DualSenseSide -eq "Right") { New-Object System.Drawing.Rectangle $dsAxis, 0, ($work.Width - $dsAxis), $work.Height } else { New-Object System.Drawing.Rectangle 700, 0, ($dsAxis + 1 - 700), $work.Height }
if ($DualSenseSide -eq "Left") { $apexRegion.Width = 700 - $apexRegion.X }
$apexHalf = $work.Clone($apexRegion, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$dsHalf = $work.Clone($dsRegion, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$work.Dispose()

[LogoClean]::KeepLargest($apexHalf, 8)
[LogoClean]::KeepLargest($dsHalf, 8)
$ab = Get-AlphaBounds $apexHalf
$db = Get-AlphaBounds $dsHalf
$apexH = $ab.Y1 - $ab.Y0 + 1
$dsH = $db.Y1 - $db.Y0 + 1
$k = $dsH / [double]$apexH
$apexW = ($ab.X1 - $ab.X0 + 1) * $k
$dsW = $db.X1 - $db.X0 + 1
$totalW = $apexW + $dsW
$f = [math]::Min($Size * (1 - 2 * $Margin) / $totalW, $Size * (1 - 2 * $Margin) / $dsH)
$left = ($Size - $totalW * $f) / 2
$top = ($Size - $dsH * $f) / 2

$apexDestW = $apexW * $f
$dsDestW = $dsW * $f
$destH = $dsH * $f
if ($DualSenseSide -eq "Right") {
    $apexX = $left; $dsX = $left + $apexDestW; $seamX = $dsX
} else {
    $dsX = $left; $apexX = $left + $dsDestW; $seamX = $apexX
}

$attr = New-Object System.Drawing.Imaging.ImageAttributes
$attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)

$body = New-Argb $Size $Size
$g = Get-Graphics $body
$g.DrawImage($apexHalf, (New-Object System.Drawing.Rectangle ([int][math]::Round($apexX)), ([int][math]::Round($top)), ([int][math]::Round($apexDestW)), ([int][math]::Round($destH))), $ab.X0, $ab.Y0, ($ab.X1 - $ab.X0 + 1), $apexH, [System.Drawing.GraphicsUnit]::Pixel, $attr)
$g.DrawImage($dsHalf, (New-Object System.Drawing.Rectangle ([int][math]::Round($dsX)), ([int][math]::Round($top)), ([int][math]::Round($dsDestW)), ([int][math]::Round($destH))), $db.X0, $db.Y0, $dsW, $dsH, [System.Drawing.GraphicsUnit]::Pixel, $attr)
$g.Dispose()
$apexHalf.Dispose(); $dsHalf.Dispose()

$seamI = [int][math]::Round($seamX)
$bb = Get-AlphaBounds $body
$yTop = $top + 2
$yBottom = $top + $destH
$cols = @()
foreach ($dx in -3, 3) {
    $cx = $seamI + $dx
    $first = -1; $last = -1
    for ($y = 0; $y -lt $Size; $y++) {
        if ($body.GetPixel($cx, $y).A -gt 128) { if ($first -lt 0) { $first = $y }; $last = $y }
    }
    $cols += , @($first, $last)
}
$yTop = [math]::Min($cols[0][0], $cols[1][0]) + 3
$yBottom = [math]::Max($cols[0][1], $cols[1][1]) - 3

$final = New-Argb $Size $Size
$g = Get-Graphics $final
if ($Background) {
    $g.Clear([System.Drawing.ColorTranslator]::FromHtml($Background))
}

if (-not $NoRim) {
    $rimColor = [System.Drawing.Color]::FromArgb(255, 214, 224, 240)
    $matrix = New-Object System.Drawing.Imaging.ColorMatrix
    $matrix.Matrix00 = 0; $matrix.Matrix11 = 0; $matrix.Matrix22 = 0
    $matrix.Matrix40 = $rimColor.R / 255.0; $matrix.Matrix41 = $rimColor.G / 255.0; $matrix.Matrix42 = $rimColor.B / 255.0
    $matrix.Matrix33 = 0.95
    $tint = New-Object System.Drawing.Imaging.ImageAttributes
    $tint.SetColorMatrix($matrix)
    $r = [math]::Max(2.0, $Size / 400.0)
    for ($i = 0; $i -lt 32; $i++) {
        $a = 2 * [math]::PI * $i / 32
        $ox = [math]::Cos($a) * $r; $oy = [math]::Sin($a) * $r
        $g.DrawImage($body, (New-Object System.Drawing.Rectangle ([int][math]::Round($ox)), ([int][math]::Round($oy)), $Size, $Size), 0, 0, $Size, $Size, [System.Drawing.GraphicsUnit]::Pixel, $tint)
    }
}
$g.DrawImage($body, 0, 0, $Size, $Size)

$line = [math]::Max(2.5, $Size / 210.0)
$brushRect = New-Object System.Drawing.RectangleF ([single]($seamX - 10)), ([single]$yTop), 20, ([single]($yBottom - $yTop))
$grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush $brushRect, ([System.Drawing.Color]::FromArgb(255, 170, 96, 255)), ([System.Drawing.Color]::FromArgb(255, 64, 214, 255)), 90.0
$blend = New-Object System.Drawing.Drawing2D.ColorBlend 3
$blend.Colors = @([System.Drawing.Color]::FromArgb(255, 170, 96, 255), [System.Drawing.Color]::FromArgb(255, 70, 130, 255), [System.Drawing.Color]::FromArgb(255, 64, 214, 255))
$blend.Positions = @(0.0, 0.5, 1.0)
$grad.InterpolationColors = $blend
foreach ($w in @(@(($line * 5), 28), @(($line * 3), 55))) {
    $glowGrad = New-Object System.Drawing.Drawing2D.LinearGradientBrush $brushRect, ([System.Drawing.Color]::FromArgb($w[1], 170, 96, 255)), ([System.Drawing.Color]::FromArgb($w[1], 64, 214, 255)), 90.0
    $gb = New-Object System.Drawing.Drawing2D.ColorBlend 3
    $gb.Colors = @([System.Drawing.Color]::FromArgb($w[1], 170, 96, 255), [System.Drawing.Color]::FromArgb($w[1], 70, 130, 255), [System.Drawing.Color]::FromArgb($w[1], 64, 214, 255))
    $gb.Positions = @(0.0, 0.5, 1.0)
    $glowGrad.InterpolationColors = $gb
    $gp = New-Object System.Drawing.Pen $glowGrad, $w[0]
    $gp.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $gp.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawLine($gp, [single]$seamX, [single]$yTop, [single]$seamX, [single]$yBottom)
}
$lp = New-Object System.Drawing.Pen $grad, $line
$lp.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $lp.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$g.DrawLine($lp, [single]$seamX, [single]$yTop, [single]$seamX, [single]$yBottom)
$hp = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(170, 235, 245, 255)), ([math]::Max(1.0, $line / 3))
$g.DrawLine($hp, [single]$seamX, [single]$yTop, [single]$seamX, [single]$yBottom)
$g.Dispose()

$final.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
$final.Dispose(); $body.Dispose()
Write-Host "Composed $Output ($Size x $Size, DualSense on the $DualSenseSide, apex scale $([math]::Round($k, 3)))"
