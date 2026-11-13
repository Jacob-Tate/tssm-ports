# Draws dist/switch/default_icon.jpg, the Switch .nro's default icon: a plain
# sponge block on a sea-blue square, 256x256 JPEG (what nx_create_nro takes).
# Shapes only, drawn here; no picture goes in. Windows PowerShell, nothing to
# install:
#   powershell -ExecutionPolicy Bypass -File tools\switch\draw_default_icon.ps1 -Out dist\switch\default_icon.jpg
# A redrawn icon changes tools/check_icon_art.py's entry for it in the same
# commit. A real picture goes through TSSM_ICON_DIR instead (docs/ICON.md).
param([string]$Out)
Add-Type -AssemblyName System.Drawing
$n = 256
$bmp = New-Object System.Drawing.Bitmap $n, $n
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
# sea blue background, as the other defaults (#1E8FD0 family)
$g.Clear([System.Drawing.Color]::FromArgb(255, 0x1E, 0x6F, 0xAF))
function RoundRect([float]$x,[float]$y,[float]$w,[float]$h,[float]$r) {
  $p = New-Object System.Drawing.Drawing2D.GraphicsPath
  $d = 2*$r
  $p.AddArc($x, $y, $d, $d, 180, 90)
  $p.AddArc($x+$w-$d, $y, $d, $d, 270, 90)
  $p.AddArc($x+$w-$d, $y+$h-$d, $d, $d, 0, 90)
  $p.AddArc($x, $y+$h-$d, $d, $d, 90, 90)
  $p.CloseFigure()
  return $p
}
# the block's shadow, the block, and its pores
$shadow = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(70, 0, 20, 40))
$g.FillPath($shadow, (RoundRect 50 58 164 164 22))
$yellow = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0xF6, 0xD3, 0x45))
$g.FillPath($yellow, (RoundRect 44 46 168 168 22))
$pore = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0xC9, 0xA3, 0x23))
foreach ($c in @(@(84,86,13), @(160,104,18), @(116,152,9), @(72,172,10), @(176,178,9), @(132,74,6), @(98,122,5))) {
  $g.FillEllipse($pore, $c[0]-$c[2], $c[1]-$c[2], 2*$c[2], 2*$c[2])
}
$g.Dispose()
$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
$ep = New-Object System.Drawing.Imaging.EncoderParameters 1
$ep.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 92L
$bmp.Save($Out, $codec, $ep)
$bmp.Dispose()
