<#
.SYNOPSIS
    Makes the port's app icons, for every platform, from one picture.

.DESCRIPTION
    tools\make_icons.ps1 -Image <picture> -OutDir <folder> [-Background '#RRGGBB']

    The repository carries no art: like the game's own files, a picture used
    as the app icon stays on the machine that builds. This script turns any
    picture (PNG, JPEG, BMP, GIF, TIFF, ICO, or WebP where Windows has the
    codec) into everything the builds take, in a folder OUTSIDE the
    repository, and the builds read that folder:

      Windows   cmake -DTSSM_ICON_DIR=<folder>        tssm.ico in tssm.exe
      Android   gradle -Ptssm.icon=<folder>           android/res overrides
                                                      the launcher icon
      Switch    nx_create_nro ... --icon=<folder>/icon.jpg
      3DS       ctr_generate_smdh ... ICON <folder>/icon-48.png

    Without the folder every build keeps its default icon. docs/ICON.md.

    The folder it writes:

      tssm.ico          16, 24, 32, 48, 64 and 256 px (PNG entries); the
                        picture centred on a transparent square
      icon-64.png       64 px, the same, for an SDL window icon
      icon.jpg          256x256 JPEG on the background colour (no alpha):
                        the Switch's NRO icon
      icon-48.png       48x48 on the background colour (opaque): the 3DS's
                        SMDH icon (smdhtool takes exactly 48x48; the SMDH
                        stores it as RGB565, without alpha)
      play-512.png      512 px on the background colour, Play Store style
      preview.png       a sheet of the above as launchers show them
      android/res/      mipmap-*dpi/ic_launcher.png (48 dp, legacy launchers),
                        mipmap-*dpi/ic_launcher_foreground.png (108 dp, the
                        picture inside the adaptive icon's 66 dp safe circle),
                        mipmap-anydpi-v26/ic_launcher.xml and
                        values/tssm_icon.xml (the background colour layer)

    Transparent margins are trimmed first, so the visible picture is what gets
    centred and sized. Scaling is done here (a Mitchell filter on
    premultiplied alpha), not by WPF; WPF only reads and writes the files.

    Switch: pass the icon to nx_create_nro when TSSM_ICON_DIR has
    icon.jpg, and leave it out otherwise (nx_create_nro then uses its default):

      if(TSSM_ICON_DIR AND EXISTS "${TSSM_ICON_DIR}/icon.jpg")
        nx_create_nro(tssm ... ICON "${TSSM_ICON_DIR}/icon.jpg")
      else()
        nx_create_nro(tssm ...)
      endif()

    Needs Windows (PresentationCore, part of .NET Framework 3.0 and later) and
    Windows PowerShell 5.1 or PowerShell 7. Nothing to install.

.PARAMETER Image
    The picture.

.PARAMETER OutDir
    Where to write. Created if missing. Refused when it is inside a git work
    tree, so the result cannot end up committed; -AllowInsideRepo overrides.

.PARAMETER Background
    The colour under the picture where a platform needs an opaque icon (the
    adaptive icon's background layer, the Switch JPEG, the Play icon).
    Default #1E8FD0, a sea blue.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\make_icons.ps1 -Image D:\art\logo.png -OutDir D:\art\icons
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Image,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Background = '#1E8FD0',
    [switch]$AllowInsideRepo
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

# Relative paths are the caller's, not this process's working directory.
$Image = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Image)
$OutDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir)

# WPF objects need a single-threaded apartment. Windows PowerShell's console
# is one; anything else runs the script again in one.
if ([Threading.Thread]::CurrentThread.GetApartmentState() -ne [Threading.ApartmentState]::STA) {
    $exe = (Get-Process -Id $PID).Path
    $relaunch = @('-NoProfile', '-NonInteractive', '-STA', '-ExecutionPolicy', 'Bypass',
        '-File', $PSCommandPath, '-Image', $Image, '-OutDir', $OutDir, '-Background', $Background)
    if ($AllowInsideRepo) { $relaunch += '-AllowInsideRepo' }
    & $exe @relaunch
    exit $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) {
    throw "make_icons: no such picture: $Image"
}
if ($Background -notmatch '^#?([0-9A-Fa-f]{6})$') {
    throw "make_icons: -Background wants #RRGGBB, not '$Background'"
}
$bgHex = $Matches[1].ToUpperInvariant()
$bgR = [Convert]::ToInt32($bgHex.Substring(0, 2), 16)
$bgG = [Convert]::ToInt32($bgHex.Substring(2, 2), 16)
$bgB = [Convert]::ToInt32($bgHex.Substring(4, 2), 16)

# --- the output stays out of every repository ---------------------------------
function Get-GitTop([string]$dir) {
    # The nearest existing folder at or above $dir; git says whether it is in
    # a work tree.
    $probe = $dir
    while ($probe -and -not (Test-Path -LiteralPath $probe -PathType Container)) {
        $probe = Split-Path -Parent $probe
    }
    if (-not $probe) { return $null }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { return $null }
    # Windows PowerShell turns a native command's stderr into an error record,
    # which 'Stop' would throw: "not a git repository" is the answer wanted.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $top = & git -C $probe rev-parse --show-toplevel 2>$null } finally { $ErrorActionPreference = $saved }
    if ($LASTEXITCODE -eq 0 -and $top) { return $top }
    return $null
}
$repoTop = Get-GitTop $OutDir
if ($repoTop -and -not $AllowInsideRepo) {
    throw ("make_icons: $OutDir is inside the git work tree $repoTop. Icon art must not be " +
        "committed; write it outside every repository (or pass -AllowInsideRepo).")
}

Add-Type -AssemblyName PresentationCore, WindowsBase

# --- the pixel work -------------------------------------------------------------
if (-not ('TssmIconPixels' -as [type])) {
    Add-Type -Language CSharp -TypeDefinition @'
using System;

public static class TssmIconPixels
{
    // Pixels are BGRA bytes, straight alpha, stride width * 4, unless a name
    // says premultiplied (then floats 0..1, B G R A, premultiplied).

    // The box around every pixel whose alpha is above thr: x0, y0, x1, y1
    // (x1 and y1 exclusive), or null when nothing is.
    public static int[] Bounds(byte[] p, int w, int h, int thr)
    {
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (p[(y * w + x) * 4 + 3] > thr)
                {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
        if (x1 < 0) return null;
        return new int[] { x0, y0, x1 + 1, y1 + 1 };
    }

    // How far the farthest corner of any pixel with alpha above thr lies from
    // (cx, cy), in pixels.
    public static double MaxRadius(byte[] p, int w, int h, int thr, double cx, double cy)
    {
        double best = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (p[(y * w + x) * 4 + 3] > thr)
                {
                    double dx = Math.Max(Math.Abs(x - cx), Math.Abs(x + 1 - cx));
                    double dy = Math.Max(Math.Abs(y - cy), Math.Abs(y + 1 - cy));
                    double d = Math.Sqrt(dx * dx + dy * dy);
                    if (d > best) best = d;
                }
        return best;
    }

    public static float[] Premultiply(byte[] p, int w, int h, int x0, int y0, int cw, int ch)
    {
        float[] o = new float[cw * ch * 4];
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++)
            {
                int s = ((y + y0) * w + (x + x0)) * 4;
                int d = (y * cw + x) * 4;
                float a = p[s + 3] / 255f;
                o[d + 0] = p[s + 0] / 255f * a;
                o[d + 1] = p[s + 1] / 255f * a;
                o[d + 2] = p[s + 2] / 255f * a;
                o[d + 3] = a;
            }
        return o;
    }

    // Mitchell-Netravali, B = C = 1/3: sharp without visible ringing.
    static double Mitchell(double x)
    {
        const double B = 1.0 / 3.0, C = 1.0 / 3.0;
        x = Math.Abs(x);
        if (x < 1)
            return ((12 - 9 * B - 6 * C) * x * x * x + (-18 + 12 * B + 6 * C) * x * x + (6 - 2 * B)) / 6;
        if (x < 2)
            return ((-B - 6 * C) * x * x * x + (6 * B + 30 * C) * x * x + (-12 * B - 48 * C) * x + (8 * B + 24 * C)) / 6;
        return 0;
    }

    // One pass along one axis: n source samples to m, each of `count` lines.
    // `step` and `line` are the float offsets between samples and lines.
    static void Pass(float[] src, float[] dst, int n, int m, int count,
                     int sStep, int sLine, int dStep, int dLine)
    {
        double scale = (double)m / n;
        double support = scale < 1 ? 2.0 / scale : 2.0;   // widen when shrinking
        double fscale = scale < 1 ? scale : 1.0;
        double[] wts = new double[(int)Math.Ceiling(support) * 2 + 2];
        for (int j = 0; j < m; j++)
        {
            double centre = (j + 0.5) / scale - 0.5;
            int first = (int)Math.Floor(centre - support) + 1;
            int last = (int)Math.Floor(centre + support);
            int k = 0;
            double sum = 0;
            for (int i = first; i <= last; i++, k++)
            {
                wts[k] = Mitchell((i - centre) * fscale);
                sum += wts[k];
            }
            for (int l = 0; l < count; l++)
            {
                double b = 0, g = 0, r = 0, a = 0;
                k = 0;
                for (int i = first; i <= last; i++, k++)
                {
                    int ii = i < 0 ? 0 : (i >= n ? n - 1 : i);
                    int s = l * sLine + ii * sStep;
                    double wt = wts[k];
                    b += src[s] * wt; g += src[s + 1] * wt; r += src[s + 2] * wt; a += src[s + 3] * wt;
                }
                int d = l * dLine + j * dStep;
                dst[d] = (float)(b / sum); dst[d + 1] = (float)(g / sum);
                dst[d + 2] = (float)(r / sum); dst[d + 3] = (float)(a / sum);
            }
        }
    }

    public static float[] Resize(float[] src, int sw, int sh, int dw, int dh)
    {
        float[] mid = new float[dw * sh * 4];
        Pass(src, mid, sw, dw, sh, 4, sw * 4, 4, dw * 4);
        float[] dst = new float[dw * dh * 4];
        Pass(mid, dst, sh, dh, dw, dw * 4, 4, dw * 4, 4);
        return dst;
    }

    // An n x n canvas, transparent or the background colour, with img
    // (premultiplied, iw x ih) over it at (ox, oy). Straight-alpha BGRA out.
    public static byte[] Compose(int n, bool opaque, int bgR, int bgG, int bgB,
                                 float[] img, int iw, int ih, int ox, int oy)
    {
        byte[] o = new byte[n * n * 4];
        float br = bgR / 255f, bg = bgG / 255f, bb = bgB / 255f;
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++)
            {
                float pb = 0, pg = 0, pr = 0, pa = 0;
                int ix = x - ox, iy = y - oy;
                if (ix >= 0 && iy >= 0 && ix < iw && iy < ih)
                {
                    int s = (iy * iw + ix) * 4;
                    pa = Clamp(img[s + 3]);
                    pb = Math.Min(Clamp(img[s]), pa);
                    pg = Math.Min(Clamp(img[s + 1]), pa);
                    pr = Math.Min(Clamp(img[s + 2]), pa);
                }
                if (opaque)
                {
                    pb += bb * (1 - pa); pg += bg * (1 - pa); pr += br * (1 - pa);
                    pa = 1;
                }
                int d = (y * n + x) * 4;
                if (pa <= 0) { o[d] = o[d + 1] = o[d + 2] = o[d + 3] = 0; continue; }
                o[d + 0] = ToByte(pb / pa);
                o[d + 1] = ToByte(pg / pa);
                o[d + 2] = ToByte(pr / pa);
                o[d + 3] = ToByte(pa);
            }
        return o;
    }

    static float Clamp(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    static byte ToByte(float v) { v = Clamp(v); return (byte)(v * 255f + 0.5f); }
}
'@
}

# --- read the picture -------------------------------------------------------------
$M = [Windows.Media.Imaging.BitmapCreateOptions]
$C = [Windows.Media.Imaging.BitmapCacheOption]
$stream = [IO.File]::OpenRead($Image)
try {
    $decoder = [Windows.Media.Imaging.BitmapDecoder]::Create($stream,
        ($M::PreservePixelFormat -bor $M::IgnoreColorProfile), $C::OnLoad)
} catch {
    throw "make_icons: Windows cannot read $Image ($($_.Exception.Message))"
} finally {
    $stream.Dispose()
}
# The largest frame (an .ico holds several sizes).
$frame = $decoder.Frames | Sort-Object { $_.PixelWidth * $_.PixelHeight } -Descending | Select-Object -First 1
$bgra = New-Object Windows.Media.Imaging.FormatConvertedBitmap($frame, [Windows.Media.PixelFormats]::Bgra32, $null, 0)
$W = $bgra.PixelWidth
$H = $bgra.PixelHeight
$pixels = New-Object byte[] ($W * $H * 4)
$bgra.CopyPixels($pixels, $W * 4, 0)

# Trim the transparent margin: alpha above 8 counts as picture.
$box = [TssmIconPixels]::Bounds($pixels, $W, $H, 8)
if ($null -eq $box) { throw "make_icons: $Image is fully transparent" }
$cx0 = $box[0]; $cy0 = $box[1]
$CW = $box[2] - $box[0]
$CH = $box[3] - $box[1]
$logo = [TssmIconPixels]::Premultiply($pixels, $W, $H, $cx0, $cy0, $CW, $CH)

# The farthest visible pixel from the trimmed picture's centre, for the
# adaptive icon's safe circle. Alpha above 64: faint anti-aliasing may cross it.
$rMax = [TssmIconPixels]::MaxRadius($pixels, $W, $H, 64, $cx0 + $CW / 2.0, $cy0 + $CH / 2.0)

Write-Host ("make_icons: {0} is {1}x{2}; the picture is {3}x{4} at ({5},{6}); background #{7}" -f
    $Image, $W, $H, $CW, $CH, $cx0, $cy0, $bgHex)

# An n x n icon with the picture `scale` times its trimmed size, centred.
function New-IconPixels([int]$n, [double]$scale, [bool]$opaque) {
    $iw = [Math]::Max(1, [int][Math]::Round($CW * $scale))
    $ih = [Math]::Max(1, [int][Math]::Round($CH * $scale))
    $img = [TssmIconPixels]::Resize($logo, $CW, $CH, $iw, $ih)
    $ox = [int][Math]::Floor(($n - $iw) / 2.0)
    $oy = [int][Math]::Floor(($n - $ih) / 2.0)
    return , ([TssmIconPixels]::Compose($n, $opaque, $bgR, $bgG, $bgB, $img, $iw, $ih, $ox, $oy))
}

# The picture fitted in a box `fill` of the icon's side.
function Get-FitScale([int]$n, [double]$fill) {
    return ($n * $fill) / [Math]::Max($CW, $CH)
}

function New-Bitmap([int]$n, [byte[]]$bytes) {
    $bmp = [Windows.Media.Imaging.BitmapSource]::Create($n, $n, 96, 96,
        [Windows.Media.PixelFormats]::Bgra32, $null, $bytes, $n * 4)
    $bmp.Freeze()
    return $bmp
}

function Get-PngBytes([Windows.Media.Imaging.BitmapSource]$bmp) {
    $enc = New-Object Windows.Media.Imaging.PngBitmapEncoder
    $enc.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bmp))
    $ms = New-Object IO.MemoryStream
    $enc.Save($ms)
    return , $ms.ToArray()
}

function Save-Png([string]$path, [Windows.Media.Imaging.BitmapSource]$bmp) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [IO.File]::WriteAllBytes($path, (Get-PngBytes $bmp))
}

function Save-Jpeg([string]$path, [Windows.Media.Imaging.BitmapSource]$bmp) {
    $rgb = New-Object Windows.Media.Imaging.FormatConvertedBitmap($bmp, [Windows.Media.PixelFormats]::Bgr24, $null, 0)
    $enc = New-Object Windows.Media.Imaging.JpegBitmapEncoder
    $enc.QualityLevel = 95
    $enc.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($rgb))
    $fs = [IO.File]::Create($path)
    try { $enc.Save($fs) } finally { $fs.Dispose() }
}

function Save-Text([string]$path, [string]$text) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding($false)))
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$written = New-Object Collections.Generic.List[string]

# --- Windows: tssm.ico, and the SDL window's 64 px ------------------------------
# The picture fills the square: Explorer and the taskbar draw icons as they are.
$icoSizes = 16, 24, 32, 48, 64, 256
$entries = @()
foreach ($n in $icoSizes) {
    $entries += , @($n, (Get-PngBytes (New-Bitmap $n (New-IconPixels $n (Get-FitScale $n 1.0) $false))))
}
$ico = New-Object IO.MemoryStream
$bw = New-Object IO.BinaryWriter($ico)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$entries.Count)   # ICONDIR
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {                                                          # ICONDIRENTRY
    $n = $e[0]; $data = $e[1]
    $dim = if ($n -ge 256) { 0 } else { $n }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$data.Length); $bw.Write([UInt32]$offset)
    $offset += $data.Length
}
foreach ($e in $entries) { $bw.Write([byte[]]$e[1]) }
$bw.Flush()
[IO.File]::WriteAllBytes((Join-Path $OutDir 'tssm.ico'), $ico.ToArray())
$written.Add('tssm.ico')

$win64 = New-Bitmap 64 (New-IconPixels 64 (Get-FitScale 64 1.0) $false)
Save-Png (Join-Path $OutDir 'icon-64.png') $win64
$written.Add('icon-64.png')

# --- Android ----------------------------------------------------------------------
$res = Join-Path $OutDir 'android\res'
$densities = [ordered]@{ mdpi = 1.0; hdpi = 1.5; xhdpi = 2.0; xxhdpi = 3.0; xxxhdpi = 4.0 }
# The adaptive foreground: 108 dp, of which launchers show the middle 72 and
# keep the middle 66 (a circle) clear of every mask. The picture's farthest
# visible pixel goes on that circle.
$fgScalePerDp = 33.0 / $rMax
foreach ($d in $densities.Keys) {
    $k = $densities[$d]
    $legacy = [int](48 * $k)
    # Legacy launchers (Android 7): the picture as it is, a 1 dp margin.
    Save-Png (Join-Path $res "mipmap-$d\ic_launcher.png") (New-Bitmap $legacy (New-IconPixels $legacy (Get-FitScale $legacy (46.0 / 48.0)) $false))
    $fg = [int](108 * $k)
    Save-Png (Join-Path $res "mipmap-$d\ic_launcher_foreground.png") (New-Bitmap $fg (New-IconPixels $fg ($fgScalePerDp * $k) $false))
    $written.Add("android/res/mipmap-$d/ic_launcher.png ($legacy px), ic_launcher_foreground.png ($fg px)")
}
Save-Text (Join-Path $res 'mipmap-anydpi-v26\ic_launcher.xml') @"
<?xml version="1.0" encoding="utf-8"?>
<!-- Made by tools/make_icons.ps1. -->
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/tssm_icon_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
"@
Save-Text (Join-Path $res 'values\tssm_icon.xml') @"
<?xml version="1.0" encoding="utf-8"?>
<!-- Made by tools/make_icons.ps1: the adaptive icon's background layer. -->
<resources>
    <color name="tssm_icon_background">#$bgHex</color>
</resources>
"@
$written.Add('android/res/mipmap-anydpi-v26/ic_launcher.xml, values/tssm_icon.xml')

# Play Store style: a full 512 px square that is the adaptive icon's visible
# middle 72 dp (Play masks it the same way).
$play = New-Bitmap 512 (New-IconPixels 512 ($fgScalePerDp * 512 / 72.0) $true)
Save-Png (Join-Path $OutDir 'play-512.png') $play
$written.Add('play-512.png')

# --- Switch: 256x256 JPEG, no alpha -------------------------------------------------
$nx = New-Bitmap 256 (New-IconPixels 256 (Get-FitScale 256 0.9) $true)
Save-Jpeg (Join-Path $OutDir 'icon.jpg') $nx
$written.Add('icon.jpg')

# --- 3DS: 48x48 PNG, opaque -----------------------------------------------------------
# smdhtool wants exactly 48x48 and stores RGB565, so the background is drawn in.
$ctr = New-Bitmap 48 (New-IconPixels 48 (Get-FitScale 48 0.9) $true)
Save-Png (Join-Path $OutDir 'icon-48.png') $ctr
$written.Add('icon-48.png')

# --- preview.png: how launchers show it -------------------------------------------
# Top row, on a light and a dark strip: the adaptive icon under a circle, a
# rounded-square and a squircle-ish mask (the middle 72 of 108 dp), the
# legacy icon, the Switch icon. Bottom: every .ico size at 1:1.
$adaptN = 432
$adaptive = New-Bitmap $adaptN (New-IconPixels $adaptN ($fgScalePerDp * 4.0) $true)
$legacyBig = New-Bitmap 192 (New-IconPixels 192 (Get-FitScale 192 (46.0 / 48.0)) $false)
$cell = 192; $gap = 24
$sheetW = $gap + 5 * ($cell + $gap)
$sheetH = $gap + 2 * ($cell + $gap) + 256 + $gap
$dv = New-Object Windows.Media.DrawingVisual
[Windows.Media.RenderOptions]::SetBitmapScalingMode($dv, [Windows.Media.BitmapScalingMode]::HighQuality)
$dc = $dv.RenderOpen()
$light = New-Object Windows.Media.SolidColorBrush([Windows.Media.Color]::FromRgb(0xEE, 0xEE, 0xEE))
$dark = New-Object Windows.Media.SolidColorBrush([Windows.Media.Color]::FromRgb(0x22, 0x22, 0x22))
$split = $gap + $cell + $gap / 2
$dc.DrawRectangle($light, $null, (New-Object Windows.Rect(0, 0, $sheetW, $split)))
$dc.DrawRectangle($dark, $null, (New-Object Windows.Rect(0, $split, $sheetW, ($sheetH - $split))))
for ($row = 0; $row -lt 2; $row++) {
    $y = $gap + $row * ($cell + $gap)
    $masks = @(
        (New-Object Windows.Media.EllipseGeometry((New-Object Windows.Rect(0, 0, $cell, $cell)))),
        (New-Object Windows.Media.RectangleGeometry((New-Object Windows.Rect(0, 0, $cell, $cell)), 28, 28)),
        (New-Object Windows.Media.RectangleGeometry((New-Object Windows.Rect(0, 0, $cell, $cell)), 70, 70))
    )
    for ($i = 0; $i -lt 3; $i++) {
        $x = $gap + $i * ($cell + $gap)
        $dc.PushTransform((New-Object Windows.Media.TranslateTransform($x, $y)))
        $dc.PushClip($masks[$i])
        # The 108 dp layer, so that its middle 72 dp is the cell.
        $full = $cell * 108.0 / 72.0
        $dc.DrawImage($adaptive, (New-Object Windows.Rect((($cell - $full) / 2), (($cell - $full) / 2), $full, $full)))
        $dc.Pop(); $dc.Pop()
    }
    $dc.DrawImage($legacyBig, (New-Object Windows.Rect(($gap + 3 * ($cell + $gap)), $y, $cell, $cell)))
    $dc.DrawImage($nx, (New-Object Windows.Rect(($gap + 4 * ($cell + $gap)), $y, $cell, $cell)))
}
$x = $gap
$y = $gap + 2 * ($cell + $gap)
foreach ($e in $entries) {
    $n = $e[0]
    $ms = New-Object IO.MemoryStream(, [byte[]]$e[1])
    $f = [Windows.Media.Imaging.BitmapFrame]::Create($ms, $M::None, $C::OnLoad)
    $dc.DrawImage($f, (New-Object Windows.Rect($x, ($y + 256 - $n), $n, $n)))
    $x += $n + $gap
}
$dc.Close()
$rtb = New-Object Windows.Media.Imaging.RenderTargetBitmap($sheetW, $sheetH, 96, 96, [Windows.Media.PixelFormats]::Pbgra32)
$rtb.Render($dv)
Save-Png (Join-Path $OutDir 'preview.png') $rtb
$written.Add('preview.png')

Write-Host "make_icons: wrote to ${OutDir}:"
foreach ($w in $written) { Write-Host "  $w" }
