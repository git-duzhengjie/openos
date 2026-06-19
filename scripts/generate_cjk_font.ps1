<#
Generate OpenOS 16x16 CJK bitmap glyph table from a Windows font via GDI+.

It scans UTF-8 source files, renders every CJK/fullwidth codepoint with
System.Drawing, and writes src/kernel/generated/cjk_font.c.
#>
param(
    [string]$FontName = 'Microsoft YaHei',
    [int]$FontSize = 14,
    [string]$Out = 'src/kernel/generated/cjk_font.c',
    [string[]]$Scan = @('src/kernel/i18n.c'),
    [string]$ExtraChars = '',
    [string]$ResourceOut = '',
    [ValidateSet('ui', 'gb2312', 'cjk-basic', 'cjk-all')]
    [string]$Coverage = 'ui',
    [switch]$Compress
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

function Add-Codepoint([System.Collections.Generic.HashSet[int]]$set, [int]$cp) {
    if ((($cp -ge 0x3400) -and ($cp -le 0x4DBF)) -or
        (($cp -ge 0x4E00) -and ($cp -le 0x9FFF)) -or
        (($cp -ge 0xF900) -and ($cp -le 0xFAFF)) -or
        (($cp -ge 0x3000) -and ($cp -le 0x303F)) -or
        (($cp -ge 0xFF00) -and ($cp -le 0xFFEF))) {
        [void]$set.Add($cp)
    }
}

function Add-TextCodepoints([System.Collections.Generic.HashSet[int]]$set, [string]$text) {
    for ($i = 0; $i -lt $text.Length; $i++) {
        $unit = [int][char]$text[$i]
        if (($unit -ge 0xD800) -and ($unit -le 0xDBFF) -and (($i + 1) -lt $text.Length)) {
            $low = [int][char]$text[$i + 1]
            if (($low -ge 0xDC00) -and ($low -le 0xDFFF)) {
                $cp = 0x10000 + (($unit - 0xD800) * 0x400) + ($low - 0xDC00)
                Add-Codepoint $set $cp
                $i++
                continue
            }
        }
        Add-Codepoint $set $unit
    }
}

function Add-CoverageCodepoints([System.Collections.Generic.HashSet[int]]$set, [string]$coverage) {
    if ($coverage -eq 'ui') {
        return
    }
    if ($coverage -eq 'gb2312') {
        for ($hi = 0xB0; $hi -le 0xF7; $hi++) {
            for ($lo = 0xA1; $lo -le 0xFE; $lo++) {
                $bytes = [byte[]]@($hi, $lo)
                try {
                    $s = [System.Text.Encoding]::GetEncoding(936).GetString($bytes)
                    if ($s.Length -gt 0) {
                        Add-Codepoint $set ([int][char]$s[0])
                    }
                } catch {
                }
            }
        }
        return
    }
    if (($coverage -eq 'cjk-basic') -or ($coverage -eq 'cjk-all')) {
        for ($cp = 0x4E00; $cp -le 0x9FFF; $cp++) {
            Add-Codepoint $set $cp
        }
        for ($cp = 0x3000; $cp -le 0x303F; $cp++) {
            Add-Codepoint $set $cp
        }
        for ($cp = 0xFF00; $cp -le 0xFFEF; $cp++) {
            Add-Codepoint $set $cp
        }
    }
    if ($coverage -eq 'cjk-all') {
        for ($cp = 0x3400; $cp -le 0x4DBF; $cp++) {
            Add-Codepoint $set $cp
        }
        for ($cp = 0xF900; $cp -le 0xFAFF; $cp++) {
            Add-Codepoint $set $cp
        }
    }
}

function Convert-ToGlyphRows([System.Drawing.Font]$font, [System.Drawing.StringFormat]$format, [int]$cp) {
    $bmp = New-Object System.Drawing.Bitmap(16, 16, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Black)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
    $rect = New-Object System.Drawing.RectangleF(0, 0, 16, 16)
    $ch = [char]$cp
    $g.DrawString([string]$ch, $font, [System.Drawing.Brushes]::White, $rect, $format)
    $rows = New-Object System.Collections.Generic.List[int]
    for ($y = 0; $y -lt 16; $y++) {
        $bits = 0
        for ($x = 0; $x -lt 16; $x++) {
            $color = $bmp.GetPixel($x, $y)
            if ($color.R -ge 96) {
                $bits = $bits -bor (0x8000 -shr $x)
            }
        }
        $rows.Add($bits)
    }
    $g.Dispose()
    $bmp.Dispose()
    return ,$rows.ToArray()
}

function Add-U32LE([System.Collections.Generic.List[byte]]$bytes, [uint32]$value) {
    $bytes.AddRange([System.BitConverter]::GetBytes($value))
}

function Add-U16LE([System.Collections.Generic.List[byte]]$bytes, [uint16]$value) {
    $bytes.AddRange([System.BitConverter]::GetBytes($value))
}

function Compress-Rle8([byte[]]$data) {
    $out = New-Object System.Collections.Generic.List[byte]
    $i = 0
    while ($i -lt $data.Length) {
        $run = 1
        while ((($i + $run) -lt $data.Length) -and ($run -lt 128) -and ($data[$i + $run] -eq $data[$i])) {
            $run++
        }
        if ($run -ge 3) {
            $out.Add([byte](0x80 -bor ($run - 1)))
            $out.Add($data[$i])
            $i += $run
            continue
        }
        $start = $i
        $i += $run
        while ($i -lt $data.Length) {
            $run = 1
            while ((($i + $run) -lt $data.Length) -and ($run -lt 128) -and ($data[$i + $run] -eq $data[$i])) {
                $run++
            }
            if (($run -ge 3) -or (($i - $start) -ge 128)) {
                break
            }
            $i += $run
        }
        $literalLen = $i - $start
        $out.Add([byte]($literalLen - 1))
        for ($j = $start; $j -lt $i; $j++) {
            $out.Add($data[$j])
        }
    }
    return ,$out.ToArray()
}

function Write-OpenOsFontResource([string]$path, [int[]]$codepoints, [hashtable]$glyphRows, [bool]$compress) {
    if ($codepoints.Length -eq 0) {
        throw 'cannot write empty CJK resource'
    }
    $sortedCps = @($codepoints) | Sort-Object -Unique
    $raw = New-Object System.Collections.Generic.List[byte]
    $ofntMagic = [uint32]0x544E464F
    $ofntzMagic = [uint32]0x5A544E4F
    Add-U32LE $raw $ofntMagic
    Add-U32LE $raw 1
    Add-U32LE $raw 1
    Add-U32LE $raw ([uint32]$sortedCps.Count)
    Add-U32LE $raw 16
    Add-U32LE $raw 16
    Add-U32LE $raw 40
    Add-U32LE $raw ([uint32](40 + ($sortedCps.Count * 2)))
    Add-U32LE $raw 32
    Add-U32LE $raw 0
    foreach ($cp in $sortedCps) {
        Add-U16LE $raw ([uint16]$cp)
    }
    foreach ($cp in $sortedCps) {
        foreach ($row in $glyphRows[$cp]) {
            Add-U16LE $raw ([uint16]$row)
        }
    }
    $bytes = $raw.ToArray()
    if ($compress) {
        $payload = Compress-Rle8 $bytes
        $out = New-Object System.Collections.Generic.List[byte]
        Add-U32LE $out $ofntzMagic
        Add-U32LE $out 1
        Add-U32LE $out 1
        Add-U32LE $out ([uint32]$bytes.Length)
        Add-U32LE $out ([uint32]$payload.Length)
        Add-U32LE $out $ofntMagic
        Add-U32LE $out 32
        Add-U32LE $out 0
        $out.AddRange($payload)
        $bytes = $out.ToArray()
    }
    $outDir = Split-Path -Parent $path
    if ($outDir -and -not (Test-Path $outDir)) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    }
    $outPath = if ([System.IO.Path]::IsPathRooted($path)) { $path } else { Join-Path (Get-Location) $path }
    [System.IO.File]::WriteAllBytes($outPath, $bytes)
    return $outPath
}

$codepoints = New-Object 'System.Collections.Generic.HashSet[int]'
foreach ($path in $Scan) {
    if (Test-Path $path) {
        $resolved = Resolve-Path $path
        $text = [System.IO.File]::ReadAllText($resolved, [System.Text.Encoding]::UTF8)
        Add-TextCodepoints $codepoints $text
    }
}
if ($ExtraChars.Length -gt 0) {
    Add-TextCodepoints $codepoints $ExtraChars
}

$fallbackCodepoints = New-Object 'System.Collections.Generic.HashSet[int]'
foreach ($cp in $codepoints) {
    [void]$fallbackCodepoints.Add($cp)
}

$resourceCodepoints = New-Object 'System.Collections.Generic.HashSet[int]'
foreach ($cp in $codepoints) {
    [void]$resourceCodepoints.Add($cp)
}
Add-CoverageCodepoints $resourceCodepoints $Coverage

$allCodepoints = New-Object 'System.Collections.Generic.HashSet[int]'
foreach ($cp in $fallbackCodepoints) {
    [void]$allCodepoints.Add($cp)
}
foreach ($cp in $resourceCodepoints) {
    [void]$allCodepoints.Add($cp)
}
$allSorted = @($allCodepoints) | Sort-Object
if ($allSorted.Count -eq 0) {
    throw 'No CJK/fullwidth codepoints found.'
}

$font = New-Object System.Drawing.Font($FontName, $FontSize, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$format = New-Object System.Drawing.StringFormat
$format.Alignment = [System.Drawing.StringAlignment]::Center
$format.LineAlignment = [System.Drawing.StringAlignment]::Center
$format.FormatFlags = $format.FormatFlags -bor [System.Drawing.StringFormatFlags]::NoClip

$glyphRows = @{}
foreach ($cp in $allSorted) {
    $glyphRows[$cp] = Convert-ToGlyphRows $font $format $cp
}

if (-not [string]::IsNullOrWhiteSpace($Out)) {
    $sorted = @($fallbackCodepoints) | Sort-Object
    if ($sorted.Count -eq 0) {
        throw 'No CJK/fullwidth codepoints found for fallback C output.'
    }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('/* Auto-generated by scripts/generate_cjk_font.ps1. Do not edit by hand. */')
    $lines.Add('#include "font.h"')
    $lines.Add('#include "cjk_font.h"')
    $lines.Add('')
    $lines.Add('const font_cjk_glyph_t g_generated_cjk_glyphs[] = {')

    foreach ($cp in $sorted) {
        $rows = New-Object System.Collections.Generic.List[string]
        foreach ($row in $glyphRows[$cp]) {
            $rows.Add(('0x{0:x4}u' -f $row))
        }
        $lines.Add(('    {{ 0x{0:X4}u, {{ {1} }} }},' -f $cp, ($rows -join ',')))
    }

    $lines.Add('};')
    $lines.Add('')
    $lines.Add('const uint32_t g_generated_cjk_glyph_count =')
    $lines.Add('    (uint32_t)(sizeof(g_generated_cjk_glyphs) / sizeof(g_generated_cjk_glyphs[0]));')
    $lines.Add('')
    $lines.Add(('const char g_generated_cjk_font_source[] = "{0}";' -f $FontName))

    $outDir = Split-Path -Parent $Out
    if ($outDir -and -not (Test-Path $outDir)) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    }
    $outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Get-Location) $Out }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($outPath, ($lines -join "`n") + "`n", $utf8NoBom)
    Write-Host ('generated fallback {0} glyphs -> {1} using {2}' -f $sorted.Count, $outPath, $FontName)
}

if (-not [string]::IsNullOrWhiteSpace($ResourceOut)) {
    $resourceSorted = @($resourceCodepoints) | Sort-Object
    $resourcePath = Write-OpenOsFontResource $ResourceOut $resourceSorted $glyphRows ([bool]$Compress)
    $mode = if ($Compress) { 'compressed resource' } else { 'resource' }
    Write-Host ('generated {0} {1} glyphs -> {2} using {3}' -f $mode, $resourceSorted.Count, $resourcePath, $FontName)
}
