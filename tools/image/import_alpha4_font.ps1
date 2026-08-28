[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$FontPath,
    [Parameter(Mandatory)][string]$OutputPath,
    [string]$PreviewPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Drawing

$fontFile = [System.IO.Path]::GetFullPath($FontPath)
if (-not [System.IO.File]::Exists($fontFile)) {
    throw "Font source '$fontFile' does not exist."
}

$privateFonts = [System.Drawing.Text.PrivateFontCollection]::new()
$privateFonts.AddFontFile($fontFile)
if ($privateFonts.Families.Count -ne 1) {
    throw "Expected one font family in '$fontFile'."
}

$cellWidth = 12
$cellHeight = 24
$glyphCount = 128
$bytesPerGlyph = $cellWidth * $cellHeight / 2
$font = [System.Drawing.Font]::new(
    $privateFonts.Families[0], 20.0,
    [System.Drawing.FontStyle]::Regular,
    [System.Drawing.GraphicsUnit]::Pixel
)
$format = [System.Drawing.StringFormat]::GenericTypographic.Clone()
$format.FormatFlags = $format.FormatFlags -bor [System.Drawing.StringFormatFlags]::MeasureTrailingSpaces
$glyphBytes = [byte[]]::new($glyphCount * $bytesPerGlyph)

try {
    for ($codepoint = 0; $codepoint -lt $glyphCount; ++$codepoint) {
        $bitmap = [System.Drawing.Bitmap]::new(
            $cellWidth, $cellHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
        )
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Black)
                $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
                $graphics.DrawString(
                    [char]$codepoint, $font, [System.Drawing.Brushes]::White,
                    [System.Drawing.PointF]::new(0.0, 0.0), $format
                )
            } finally {
                $graphics.Dispose()
            }

            $glyphOffset = $codepoint * $bytesPerGlyph
            for ($y = 0; $y -lt $cellHeight; ++$y) {
                for ($x = 0; $x -lt $cellWidth; $x += 2) {
                    $first = [Math]::Min(15, [int](($bitmap.GetPixel($x, $y).R + 8) / 17))
                    $second = [Math]::Min(15, [int](($bitmap.GetPixel($x + 1, $y).R + 8) / 17))
                    $glyphBytes[$glyphOffset + ($y * $cellWidth + $x) / 2] =
                        [byte](($first -shl 4) -bor $second)
                }
            }
        } finally {
            $bitmap.Dispose()
        }
    }

    $encoded = [System.Collections.Generic.List[byte]]::new()
    for ($index = 0; $index -lt $glyphBytes.Length;) {
        $value = $glyphBytes[$index]
        $count = 1
        while ($index + $count -lt $glyphBytes.Length -and
            $glyphBytes[$index + $count] -eq $value -and $count -lt 255) {
            ++$count
        }
        $encoded.Add([byte]$count)
        $encoded.Add($value)
        $index += $count
    }

    $base64 = [Convert]::ToBase64String($encoded.ToArray(), 'InsertLineBreaks') + "`r`n"
    $outputFile = [System.IO.Path]::GetFullPath($OutputPath)
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $outputFile)) | Out-Null
    [System.IO.File]::WriteAllText($outputFile, $base64, [System.Text.Encoding]::ASCII)

    if (-not [string]::IsNullOrWhiteSpace($PreviewPath)) {
        $sample = 'InferenceOS> help  Aa Bb Gg 0123 {}[]'
        $preview = [System.Drawing.Bitmap]::new($sample.Length * $cellWidth, $cellHeight)
        try {
            for ($index = 0; $index -lt $sample.Length; ++$index) {
                $glyphOffset = [int][char]$sample[$index] * $bytesPerGlyph
                for ($y = 0; $y -lt $cellHeight; ++$y) {
                    for ($x = 0; $x -lt $cellWidth; ++$x) {
                        $packed = $glyphBytes[$glyphOffset + ($y * $cellWidth + $x) / 2]
                        $coverage = if (($x -band 1) -eq 0) { $packed -shr 4 } else { $packed -band 15 }
                        $value = 16 + [int](224 * $coverage / 15)
                        $preview.SetPixel($index * $cellWidth + $x, $y,
                            [System.Drawing.Color]::FromArgb($value, $value, $value))
                    }
                }
            }
            $previewFile = [System.IO.Path]::GetFullPath($PreviewPath)
            [System.IO.Directory]::CreateDirectory((Split-Path -Parent $previewFile)) | Out-Null
            $preview.Save($previewFile, [System.Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $preview.Dispose()
        }
    }

    Write-Host "Imported $glyphCount glyphs ($($glyphBytes.Length) bytes) into $($encoded.Count) RLE bytes."
} finally {
    $format.Dispose()
    $font.Dispose()
    $privateFonts.Dispose()
}
