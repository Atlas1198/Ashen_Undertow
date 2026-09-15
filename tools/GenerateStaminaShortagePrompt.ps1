Add-Type -AssemblyName System.Drawing

$outputPath = Join-Path $PSScriptRoot '..\assets\texture\ui\charge_stamina_shortage.png'
$bitmap = [System.Drawing.Bitmap]::new(900, 110,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::Transparent)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

$font = [System.Drawing.Font]::new('Yu Gothic UI', 42,
    [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$format = [System.Drawing.StringFormat]::new()
$format.Alignment = [System.Drawing.StringAlignment]::Center
$format.LineAlignment = [System.Drawing.StringAlignment]::Center
$bounds = [System.Drawing.RectangleF]::new(0, 0, 900, 110)
$glow = [System.Drawing.SolidBrush]::new(
    [System.Drawing.Color]::FromArgb(160, 0, 175, 255))
$outline = [System.Drawing.SolidBrush]::new(
    [System.Drawing.Color]::FromArgb(255, 0, 8, 16))
$white = [System.Drawing.SolidBrush]::new(
    [System.Drawing.Color]::FromArgb(255, 250, 253, 255))
$text = -join @(
    [char]0x6E9C, [char]0x3081, [char]0x653B, [char]0x6483, [char]0x306E,
    [char]0x30B9, [char]0x30BF, [char]0x30DF, [char]0x30CA, [char]0x304C,
    [char]0x8DB3, [char]0x308A, [char]0x307E, [char]0x305B, [char]0x3093)

foreach ($offset in @(@(-4, 0), @(4, 0), @(0, -4), @(0, 4))) {
    $shifted = [System.Drawing.RectangleF]::new(
        $offset[0], $offset[1], 900, 110)
    $graphics.DrawString($text, $font, $glow, $shifted, $format)
}
foreach ($offset in @(@(-2, 0), @(2, 0), @(0, -2), @(0, 2))) {
    $shifted = [System.Drawing.RectangleF]::new(
        $offset[0], $offset[1], 900, 110)
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
