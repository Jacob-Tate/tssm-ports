# Draws dist/3ds/cia_banner.png (256x128, the HOME Menu's top-screen banner) and
# dist/3ds/cia_icon.png (the .cia's icon): the port's own sponge block
# (tools/switch/draw_default_icon.ps1's shapes) on sea blue, and the name.
# Shapes and text only; no picture goes in. Windows PowerShell:
#   powershell -ExecutionPolicy Bypass -File tools\3ds\draw_cia_art.ps1 -OutDir dist\3ds
# The TSSM Dehydrated release's banner (docs/3DS.md) is the same picture with
# "Dehydrated" under the name:
#   powershell -ExecutionPolicy Bypass -File tools\3ds\draw_cia_art.ps1 -OutDir dist\3ds `
#     -Subtitle Dehydrated -BannerFile cia_banner_dehydrated.png -BannerOnly
param([string]$OutDir, [string]$Subtitle = '(port)', [string]$BannerFile = 'cia_banner.png',
      [switch]$BannerOnly)
Add-Type -AssemblyName System.Drawing

function RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
  $p = New-Object System.Drawing.Drawing2D.GraphicsPath
  $d = 2 * $r
  $p.AddArc($x, $y, $d, $d, 180, 90)
  $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
  $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
  $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
  $p.CloseFigure()
  return $p
}

# The block as in the Switch default, in a 256-unit box scaled by $s at ($ox, $oy).
function DrawBlock($g, [float]$ox, [float]$oy, [float]$s) {
  $shadow = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(70, 0, 20, 40))
  $g.FillPath($shadow, (RoundRect ($ox + 50 * $s) ($oy + 58 * $s) (164 * $s) (164 * $s) (22 * $s)))
  $yellow = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0xF6, 0xD3, 0x45))
  $g.FillPath($yellow, (RoundRect ($ox + 44 * $s) ($oy + 46 * $s) (168 * $s) (168 * $s) (22 * $s)))
  $pore = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0xC9, 0xA3, 0x23))
  foreach ($c in @(@(84, 86, 13), @(160, 104, 18), @(116, 152, 9), @(72, 172, 10), @(176, 178, 9), @(132, 74, 6), @(98, 122, 5))) {
    $g.FillEllipse($pore, $ox + ($c[0] - $c[2]) * $s, $oy + ($c[1] - $c[2]) * $s, 2 * $c[2] * $s, 2 * $c[2] * $s)
  }
}

$blue = [System.Drawing.Color]::FromArgb(255, 0x1E, 0x6F, 0xAF)

# icon: 48x48, the block filling the square
if (-not $BannerOnly) {
  $ic = New-Object System.Drawing.Bitmap 48, 48
  $g = [System.Drawing.Graphics]::FromImage($ic)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear($blue)
  DrawBlock $g 0 0 (48.0 / 256.0)
  $g.Dispose()
  $ic.Save((Join-Path $OutDir 'cia_icon.png'), [System.Drawing.Imaging.ImageFormat]::Png)
  $ic.Dispose()
}

# banner: 256x128, the block on the left, the title on the right
$bn = New-Object System.Drawing.Bitmap 256, 128
$g = [System.Drawing.Graphics]::FromImage($bn)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.Clear($blue)
DrawBlock $g 0 0 0.5
$white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
$f1 = New-Object System.Drawing.Font 'Segoe UI', 26, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
$f2 = New-Object System.Drawing.Font 'Segoe UI', 15, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$g.DrawString('TSSM', $f1, $white, 128, 36)
$g.DrawString($Subtitle, $f2, $white, 131, 68)
$g.Dispose()
$bn.Save((Join-Path $OutDir $BannerFile), [System.Drawing.Imaging.ImageFormat]::Png)
$bn.Dispose()
