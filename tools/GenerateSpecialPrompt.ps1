Add-Type -AssemblyName System.Drawing

$outputPath = Join-Path $PSScriptRoot '..\assets\texture\ui\use_special_attack.png'
$bitmap = [System.Drawing.Bitmap]::new(700, 120,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::Transparent)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

$font = [System.Drawing.Font]::new('Yu Gothic UI', 52,
    [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$format = [System.Drawing.StringFormat]::new()
$format.Alignment = [System.Drawing.StringAlignment]::Center
$format.LineAlignment = [System.Drawing.StringAlignment]::Center
$bounds = [System.Drawing.RectangleF]::new(0, 0, 700, 120)
$glow = [System.Drawing.SolidBrush]::new(
    [System.Drawing.Color]::FromArgb(170, 0, 175, 255))
$outline = [System.Drawing.SolidBrush]::new(
    [System.Drawing.Color]::FromArgb(255, 0, 8, 16))
$white = [System.Drawing.SolidBrush]::new(
    [System.Drawing.Color]::FromArgb(255, 250, 253, 255))
$text = -join @(
    [char]0x5FC5, [char]0x6BBA, [char]0x6280, [char]0x3092,
    [char]0x6483, [char]0x3066, [char]0xFF01)

foreach ($offset in @(@(-5, 0), @(5, 0), @(0, -5), @(0, 5))) {
    $shifted = [System.Drawing.RectangleF]::new(
        $offset[0], $offset[1], 700, 120)
    $graphics.DrawString($text, $font, $glow, $shifted, $format)
}
foreach ($offset in @(@(-2, 0), @(2, 0), @(0, -2), @(0, 2))) {
    $shifted = [System.Drawing.RectangleF]::new(
        $offset[0], $offset[1], 700, 120)
    $graphics.DrawString($text, $font, $outline, $shifted, $format)
}
$graphics.DrawString($text, $font, $white, $bounds, $format)
$bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)

$white.Dispose()
$outline.Dispose()
$glow.Dispose()
$format.Dispose()
$font.Dispose()
$graphics.Dispose()
$bitmap.Dispose()
