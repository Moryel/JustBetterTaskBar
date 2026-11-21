# Generates the Just Better TaskBar app icon: JustBetterTaskBar.ico (16-256 px) and icon_preview.png
# It shares the look of the dock's Start icon: rounded square, dark gray-blue gradient,
# thin light outline and the same palette, with a small dock drawn inside.
# Usage:  powershell -ExecutionPolicy Bypass -File make_icon.ps1
Add-Type -AssemblyName System.Drawing
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path

function RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $r = [Math]::Min($r, [Math]::Min($w, $h) / 2); $d = $r * 2
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure(); return $p
}
function C($a, $r, $g, $b) { [System.Drawing.Color]::FromArgb($a, $r, $g, $b) }

function DrawIcon([int]$sz) {
    $bmp = New-Object System.Drawing.Bitmap $sz, $sz, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'; $g.Clear([System.Drawing.Color]::Transparent)
    # background: same size and colors as the Start icon
    $m = $sz * 0.07; $w = $sz - 2 * $m
    $base = RoundRect $m $m $w $w ($sz * 0.23)
    $lg = New-Object System.Drawing.Drawing2D.LinearGradientBrush ((New-Object System.Drawing.PointF 0, $m), (New-Object System.Drawing.PointF 0, ($sz - $m)), (C 255 86 90 110), (C 255 36 38 50))
    $g.FillPath($lg, $base)
    $pen = New-Object System.Drawing.Pen (C 70 255 255 255), ([Math]::Max(1.0, $sz / 70.0))
    $g.DrawPath($pen, $base)
    # frosted shelf
    $sx = $m + $w * 0.10; $sw = $w * 0.80; $sh = $w * 0.22; $sy = $m + $w * 0.62
    $shelf = RoundRect $sx $sy $sw $sh ($sh * 0.38)
    $g.FillPath((New-Object System.Drawing.SolidBrush (C 70 255 255 255)), $shelf)
    if ($sz -ge 32) { $g.DrawPath((New-Object System.Drawing.Pen (C 110 255 255 255), ([Math]::Max(1.0, $sz / 128.0))), $shelf) }
    # tiles on the shelf (the middle one magnified), colors from the Start icon palette
    $t = $w * 0.17; $tb = $t * 1.45; $gap = $t * 0.22
    $total = $t + $gap + $tb + $gap + $t
    $x0 = $m + ($w - $total) / 2; $bottom = $sy + $sh - $sh * 0.22
    $tiles = @(
        @($x0, $t, (C 255 255 95 87)),
        @(($x0 + $t + $gap), $tb, (C 255 10 132 255)),
        @(($x0 + $t + $gap + $tb + $gap), $t, (C 255 40 201 64))
    )
    foreach ($ti in $tiles) {
        $tp = RoundRect $ti[0] ($bottom - $ti[1]) $ti[1] $ti[1] ($ti[1] * 0.26)
        $g.FillPath((New-Object System.Drawing.SolidBrush $ti[2]), $tp)
    }
    # running indicators (larger sizes only)
    if ($sz -ge 48) {
        $dr = [Math]::Max(1.0, $sz / 80.0); $dy = $sy + $sh - $sh * 0.11
        foreach ($cx in @(($x0 + $t / 2), ($x0 + $t + $gap + $tb / 2))) {
            $g.FillEllipse((New-Object System.Drawing.SolidBrush (C 230 240 240 245)), ($cx - $dr), ($dy - $dr), (2 * $dr), (2 * $dr))
        }
    }
    $g.Dispose()
    return $bmp
}

# ICO file: every size is stored as PNG (supported since Windows Vista)
$sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256
$pngs = foreach ($s in $sizes) { $b = DrawIcon $s; $ms = New-Object System.IO.MemoryStream; $b.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png); if ($s -eq 256) { $b.Save((Join-Path $dir 'icon_preview.png')) }; $b.Dispose(); , $ms.ToArray() }
$fs = [System.IO.File]::Create((Join-Path $dir 'JustBetterTaskBar.ico'))
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $dim = if ($s -ge 256) { 0 } else { $s }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]$pngs[$i].Length); $bw.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $bw.Write($p) }
$bw.Close()
"Created JustBetterTaskBar.ico ($($sizes -join ', ') px)"
