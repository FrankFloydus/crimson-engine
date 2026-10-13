param(
    [string]$InputPng = (Join-Path $PSScriptRoot '..\assets\branding\crimson-engine.png'),
    [string]$OutputIco = (Join-Path $PSScriptRoot '..\assets\branding\crimson-engine.ico')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$InputPng = [IO.Path]::GetFullPath($InputPng)
$OutputIco = [IO.Path]::GetFullPath($OutputIco)
if ([string]::Equals($InputPng, $OutputIco, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Input PNG and output ICO must be different files.'
}

function Get-ArtworkBounds([System.Drawing.Bitmap]$Source) {
    $left = $Source.Width
    $top = $Source.Height
    $right = -1
    $bottom = -1
    for ($y = 0; $y -lt $Source.Height; $y++) {
        for ($x = 0; $x -lt $Source.Width; $x++) {
            # Ignore nearly invisible generation/resampling specks, which would
            # otherwise expand the crop and make the taskbar emblem too small.
            if ($Source.GetPixel($x, $y).A -lt 8) { continue }
            $left = [Math]::Min($left, $x)
            $top = [Math]::Min($top, $y)
            $right = [Math]::Max($right, $x)
            $bottom = [Math]::Max($bottom, $y)
        }
    }
    if ($right -lt $left) { throw 'The source PNG contains no visible artwork.' }
    # Retain the faint antialiased boundary around the meaningful artwork.
    $left = [Math]::Max(0, $left - 2)
    $top = [Math]::Max(0, $top - 2)
    $right = [Math]::Min($Source.Width - 1, $right + 2)
    $bottom = [Math]::Min($Source.Height - 1, $bottom + 2)
    return [Drawing.RectangleF]::new($left, $top, $right - $left + 1, $bottom - $top + 1)
}

function Convert-IconImage([System.Drawing.Image]$Source, [Drawing.RectangleF]$ArtworkBounds, [int]$Size) {
    $bitmap = [Drawing.Bitmap]::new($Size, $Size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        $graphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        # Fit the visible mark, not the source canvas. A fractional-pixel inset
        # keeps pointed ends intact while giving small shell icons enough weight.
        $inset = [Math]::Max(0.25, $Size * 0.01)
        $scale = ($Size - 2 * $inset) / [Math]::Max($ArtworkBounds.Width, $ArtworkBounds.Height)
        $width = $ArtworkBounds.Width * $scale
        $height = $ArtworkBounds.Height * $scale
        $destination = [Drawing.RectangleF]::new(($Size - $width) / 2, ($Size - $height) / 2, $width, $height)
        $graphics.DrawImage($Source, $destination, $ArtworkBounds, [Drawing.GraphicsUnit]::Pixel)
        $graphics.Flush()
        if ($Size -eq 256) {
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
        } else {
            # ICO DIBs contain a double-height header, bottom-up BGRA pixels, and an AND mask.
            $writer.Write([uint32]40)
            $writer.Write([int32]$Size)
            $writer.Write([int32]($Size * 2))
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]0)
            $writer.Write([uint32]($Size * $Size * 4))
            for ($field = 0; $field -lt 4; $field++) { $writer.Write([uint32]0) }
            for ($y = $Size - 1; $y -ge 0; $y--) {
                for ($x = 0; $x -lt $Size; $x++) {
                    $pixel = $bitmap.GetPixel($x, $y)
                    $writer.Write([byte]$pixel.B)
                    $writer.Write([byte]$pixel.G)
                    $writer.Write([byte]$pixel.R)
                    $writer.Write([byte]$pixel.A)
                }
            }
            $maskStride = [int]([Math]::Ceiling($Size / 32.0) * 4)
            for ($y = $Size - 1; $y -ge 0; $y--) {
                $row = [byte[]]::new($maskStride)
                for ($x = 0; $x -lt $Size; $x++) {
                    if ($bitmap.GetPixel($x, $y).A -eq 0) {
                        $row[$x -shr 3] = $row[$x -shr 3] -bor (128 -shr ($x -band 7))
                    }
                }
                $writer.Write($row)
            }
        }
        $writer.Flush()
        return ,$stream.ToArray()
    } finally {
        $writer.Dispose()
        $stream.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$source = [Drawing.Image]::FromFile($InputPng)
$ico = [IO.MemoryStream]::new()
$output = [IO.BinaryWriter]::new($ico)
try {
    if ($source.RawFormat.Guid -ne [Drawing.Imaging.ImageFormat]::Png.Guid -or
        $source.Width -ne 512 -or $source.Height -ne 512 -or
        -not [Drawing.Image]::IsAlphaPixelFormat($source.PixelFormat)) {
        throw 'Expected a 512x512 PNG with an alpha channel.'
    }
    $sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
    $artworkBounds = Get-ArtworkBounds $source
    $images = [Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sizes) { $images.Add((Convert-IconImage $source $artworkBounds $size)) }
    $output.Write([uint16]0)
    $output.Write([uint16]1)
    $output.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index = 0; $index -lt $sizes.Count; $index++) {
        $dimension = [byte]($sizes[$index] % 256)
        $output.Write($dimension)
        $output.Write($dimension)
        $output.Write([byte]0)
        $output.Write([byte]0)
        $output.Write([uint16]1)
        $output.Write([uint16]32)
        $output.Write([uint32]$images[$index].Length)
        $output.Write([uint32]$offset)
        $offset += $images[$index].Length
    }
    foreach ($image in $images) { $output.Write($image) }
    $output.Flush()
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($OutputIco)) | Out-Null
    [IO.File]::WriteAllBytes($OutputIco, $ico.ToArray())
    Write-Host "Created $OutputIco ($($sizes -join ', ') pixels); source PNG unchanged."
} finally {
    $output.Dispose()
    $ico.Dispose()
    $source.Dispose()
}
