Add-Type -AssemblyName System.Drawing

$outputPath = Join-Path $PSScriptRoot '..\assets\texture\ui\tutorial_steps_ja.png'
$bitmap = [System.Drawing.Bitmap]::new(640, 880,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::Transparent)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

$headingFont = [System.Drawing.Font]::new('Yu Gothic UI', 34,
    [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$keyFont = [System.Drawing.Font]::new('Yu Gothic UI', 22,
    [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$white = [System.Drawing.SolidBrush]::new(
    [System.Drawing.Color]::FromArgb(255, 250, 253, 255))
$cyan = [System.Drawing.SolidBrush]::new(
    [System.Drawing.Color]::FromArgb(255, 95, 235, 255))
$glow = [System.Drawing.SolidBrush]::new(
    [System.Drawing.Color]::FromArgb(180, 0, 175, 255))
$outline = [System.Drawing.SolidBrush]::new(
    [System.Drawing.Color]::FromArgb(235, 0, 8, 16))

$items = @(
    @('歩く', 'WASD'),
    @('ジャンプ', 'Space'),
    @('走る', 'WASD + Shift'),
    @('回避', '走る + Space'),
    @('連続攻撃', '左クリック三連打'),
    @('溜め攻撃', '左クリック長押し'),
    @('ガード', '右クリック長押し'),
    @('必殺技', '左右同時クリック')
)

for ($i = 0; $i -lt $items.Count; ++$i) {
    $y = 16 + $i * 108
    foreach ($offset in @(@(-2, 0), @(2, 0), @(0, -2), @(0, 2))) {
        $graphics.DrawString($items[$i][0], $headingFont, $outline,
            92 + $offset[0], $y + $offset[1])
        $graphics.DrawString($items[$i][1], $keyFont, $outline,
            94 + $offset[0], $y + 50 + $offset[1])
    }
    $graphics.DrawString($items[$i][0], $headingFont, $glow, 95, $y + 3)
    $graphics.DrawString($items[$i][0], $headingFont, $white, 92, $y)
    $graphics.DrawString($items[$i][1], $keyFont, $cyan, 94, $y + 50)
}

$bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$glow.Dispose()
$outline.Dispose()
$cyan.Dispose()
$white.Dispose()
$keyFont.Dispose()
$headingFont.Dispose()
$graphics.Dispose()
$bitmap.Dispose()
