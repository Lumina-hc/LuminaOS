$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$CPS = @(
    0x6587, 0x4EF6, 0x8BB0, 0x4E8B, 0x5173, 0x4E8E, 0x7CFB, 0x7EDF,
    0x91CD, 0x542F, 0x673A, 0x6B22, 0x8FCE, 0x4F7F, 0x7528, 0x64CD,
    0x4F5C, 0x9000, 0x51FA, 0x95ED, 0x672C
)

if ($args.Count -ge 1) {
    $OUT = $args[0]
} else {
    $OUT = Join-Path $PSScriptRoot "..\build\font16.bin"
}
$OUT = [System.IO.Path]::GetFullPath($OUT)
$dir = [System.IO.Path]::GetDirectoryName($OUT)
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }

$font = New-Object System.Drawing.Font("SimSun", 16, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$bmp = $null
$g = $null
$stream = $null
try {
    $stream = [System.IO.File]::Create($OUT)
    $bw = New-Object System.IO.BinaryWriter($stream)
    $bw.Write([byte]0x4C)
    $bw.Write([byte]0x5A)
    $bw.Write([byte]0x46)
    $bw.Write([byte]0x31)
    $bw.Write([uint16]$CPS.Count)

    foreach ($cp in $CPS) {
        $ch = [char]$cp
        $bmp = New-Object System.Drawing.Bitmap(16, 16)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.Clear([System.Drawing.Color]::White)
        $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment = [System.Drawing.StringAlignment]::Center
        $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
        $g.DrawString($ch.ToString(), $font, [System.Drawing.Brushes]::Black,
                      (New-Object System.Drawing.RectangleF(0, 0, 16, 16)), $sf)
        $sf.Dispose()
        $g.Dispose()
        $g = $null

        $bw.Write([uint32]$cp)
        for ($y = 0; $y -lt 16; $y++) {
            $word = 0
            for ($x = 0; $x -lt 16; $x++) {
                $p = $bmp.GetPixel($x, $y)
                $on = 0
                if ($p.R -lt 160) { $on = 1 }
                $word = ($word -shl 1) -bor $on
            }
            $bw.Write([uint16]$word)
        }
        $bmp.Dispose()
        $bmp = $null
    }
    $bw.Flush()
    $stream.Flush()
    Write-Host "font16.bin: $((Get-Item $OUT).Length) bytes, $($CPS.Count) glyphs -> $OUT"
}
finally {
    if ($g) { $g.Dispose() }
    if ($bmp) { $bmp.Dispose() }
    if ($stream) { $stream.Close() }
    $font.Dispose()
}
