Add-Type -AssemblyName System.Drawing

$outputDirectory = Join-Path $PSScriptRoot '..\assets\texture\ui'
$font = [System.Drawing.Font]::new('Perpetua Titling MT', 40,
    [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)

$labels = @(
    @('hud_life.png', 'VITALITY', 255, 75, 90),
    @('hud_stamina.png', 'STAMINA', 255, 205, 65),
    @('hud_special.png', 'ULTIMATE', 70, 220, 255),
    @('hud_boss.png', 'BOSS', 255, 65, 75)
)

foreach ($label in $labels) {
    $measureBitmap = [System.Drawing.Bitmap]::new(1, 1)
    $measureGraphics = [System.Drawing.Graphics]::FromImage($measureBitmap)
    $textSize = $measureGraphics.MeasureString($label[1], $font)
    $width = [Math]::Ceiling($textSize.Width) + 28
    $measureGraphics.Dispose()
    $measureBitmap.Dispose()
    $bitmap = [System.Drawing.Bitmap]::new($width, 76,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $outline = [System.Drawing.SolidBrush]::new(
        [System.Drawing.Color]::FromArgb(245, 0, 6, 12))
    $glow = [System.Drawing.SolidBrush]::new(
        [System.Drawing.Color]::FromArgb(145, $label[2], $label[3], $label[4]))
    $fill = [System.Drawing.SolidBrush]::new(
        [System.Drawing.Color]::FromArgb(255, 235, 240, 245))
    foreach ($offset in @(@(-2, 0), @(2, 0), @(0, -2), @(0, 2))) {
        $graphics.DrawString($label[1], $font, $outline,
            12 + $offset[0], 9 + $offset[1])
    }
    $graphics.DrawString($label[1], $font, $glow, 15, 12)
    $graphics.DrawString($label[1], $font, $fill, 12, 9)
    $bitmap.Save((Join-Path $outputDirectory $label[0]),
        [System.Drawing.Imaging.ImageFormat]::Png)
    $fill.Dispose()
    $glow.Dispose()
    $outline.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}

$font.Dispose()
