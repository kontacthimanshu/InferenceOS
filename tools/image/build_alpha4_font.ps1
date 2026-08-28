[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourcePath,
    [Parameter(Mandatory)][string]$OutputPath,
    [Parameter(Mandatory)][string]$CSourcePath,
    [Parameter(Mandatory)][string]$LicensePath,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

foreach ($required in @($SourcePath, $LicensePath)) {
    if (-not [System.IO.File]::Exists($required)) {
        throw "Required font input '$required' does not exist."
    }
}

$encoded = [Convert]::FromBase64String(
    [System.IO.File]::ReadAllText([System.IO.Path]::GetFullPath($SourcePath))
)
if (($encoded.Length -band 1) -ne 0) {
    throw 'The alpha-font RLE stream must contain count/value byte pairs.'
}

$glyphData = [System.Collections.Generic.List[byte]]::new(18432)
for ($index = 0; $index -lt $encoded.Length; $index += 2) {
    $count = $encoded[$index]
    if ($count -eq 0) { throw 'The alpha-font RLE stream contains a zero run.' }
    for ($run = 0; $run -lt $count; ++$run) {
        $glyphData.Add($encoded[$index + 1])
    }
    if ($glyphData.Count -gt 18432) {
        throw 'The alpha-font RLE stream expands beyond the required glyph size.'
    }
}
if ($glyphData.Count -ne 18432) {
    throw "The alpha-font RLE stream expands to $($glyphData.Count), expected 18432 bytes."
}

[byte[]]$bytes = [byte[]]::new(32 + $glyphData.Count)
function Set-U32([int]$Offset, [uint32]$Value) {
    for ($index = 0; $index -lt 4; ++$index) {
        $bytes[$Offset + $index] = ($Value -shr (8 * $index)) -band 0xff
    }
}
Set-U32 0 0x34464149
Set-U32 4 0
Set-U32 8 32
Set-U32 12 0
Set-U32 16 128
Set-U32 20 144
Set-U32 24 24
Set-U32 28 12
$glyphData.CopyTo($bytes, 32)

$output = [System.IO.Path]::GetFullPath($OutputPath)
$cSource = [System.IO.Path]::GetFullPath($CSourcePath)
foreach ($path in @($output, $cSource)) {
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
    if ([System.IO.File]::Exists($path) -and -not $Force) {
        throw "Generated font output '$path' exists; pass -Force to replace it."
    }
}
[System.IO.File]::WriteAllBytes($output, $bytes)

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('/* Generated deterministically by tools/image/build_alpha4_font.ps1. */')
$lines.Add('const unsigned char inferenceos_font_asset[]')
$lines.Add('    __attribute__((section(".inferenceos_font"), used, aligned(16))) = {')
for ($offset = 0; $offset -lt $bytes.Length; $offset += 12) {
    $last = [Math]::Min($offset + 12, $bytes.Length)
    $values = for ($index = $offset; $index -lt $last; ++$index) {
        '0x{0:x2}' -f $bytes[$index]
    }
    $lines.Add('    ' + ($values -join ', ') + ',')
}
$lines.Add('};')
$lines.Add('')
[System.IO.File]::WriteAllText(
    $cSource, ($lines -join "`n"), [System.Text.UTF8Encoding]::new($false)
)

Write-Output $output
