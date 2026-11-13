<#
.SYNOPSIS
    Makes the co-op program's Switch icon: the normal icon with a CO-OP band.

.DESCRIPTION
    tools\switch\make_coop_icon.ps1 -Image <icon.jpg> -Out <icon-coop.jpg>

    The co-op .nro sits beside tssm.nro in the Homebrew Menu, so its icon must
    tell the two apart at a glance: the same 256x256 picture with a red band
    and "CO-OP" across its lower part. CMakeLists.txt takes
    <TSSM_ICON_DIR>/icon-coop.jpg when it exists (run this on the folder
    tools\make_icons.ps1 made), else dist/switch/default_icon_coop.jpg, which
    is this script run on dist/switch/default_icon.jpg.

    Needs Windows (System.Drawing, part of .NET Framework). Nothing to install.
#>
param(
    [Parameter(Mandatory = $true)][string]$Image,
    [Parameter(Mandatory = $true)][string]$Out
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src = [System.Drawing.Image]::FromFile((Resolve-Path $Image).Path)
try {
    $size = 256
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.DrawImage($src, 0, 0, $size, $size)

    # The band: across the lower third, with a white rule above and below.
    $bandTop = 176
    $bandHeight = 64
    $band = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(235, 200, 30, 45))
    $g.FillRectangle($band, 0, $bandTop, $size, $bandHeight)
    $rule = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), 4
    $g.DrawLine($rule, 0, $bandTop, $size, $bandTop)
    $g.DrawLine($rule, 0, $bandTop + $bandHeight, $size, $bandTop + $bandHeight)

    $font = New-Object System.Drawing.Font 'Arial Black', 34, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = New-Object System.Drawing.RectangleF 0, $bandTop, $size, $bandHeight
    $shadow = New-Object System.Drawing.RectangleF 2, ($bandTop + 2), $size, $bandHeight
    $g.DrawString('CO-OP', $font, (New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(140, 0, 0, 0))), $shadow, $format)
    $g.DrawString('CO-OP', $font, [System.Drawing.Brushes]::White, $rect, $format)
    $g.Dispose()

    # A baseline JPEG at quality 95: what nx_create_nro takes.
    $codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
    $params = New-Object System.Drawing.Imaging.EncoderParameters 1
    $params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 95L
    $full = [System.IO.Path]::GetFullPath($Out)
    $bmp.Save($full, $codec, $params)
    $bmp.Dispose()
    Write-Output "Wrote $full"
}
finally {
    $src.Dispose()
}
