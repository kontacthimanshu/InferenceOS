[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputPath,
    [Parameter(Mandatory)][string]$CSourcePath,
    [Parameter(Mandatory)][string]$LicensePath,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not [System.IO.File]::Exists($LicensePath)) {
    throw "Font license '$LicensePath' does not exist."
}

# Project-authored 5x7 column glyphs. Lowercase characters intentionally use
# their uppercase form; the resulting small display face remains legible while
# keeping its source compact and auditable.
$patterns = @{
    ' ' = '0000000000'; '!' = '00005F0000'; '"' = '0007000700'
    '#' = '147F147F14'; '$' = '242A7F2A12'; '%' = '2313086462'
    '&' = '3649552250'; "'" = '0005030000'; '(' = '001C224100'
    ')' = '0041221C00'; '*' = '14083E0814'; '+' = '08083E0808'
    ',' = '0050300000'; '-' = '0808080808'; '.' = '0060600000'
    '/' = '2010080402'; '0' = '3E5149453E'; '1' = '00427F4000'
    '2' = '4261514946'; '3' = '2141454B31'; '4' = '1814127F10'
    '5' = '2745454539'; '6' = '3C4A494930'; '7' = '0171090503'
    '8' = '3649494936'; '9' = '064949291E'; ':' = '0036360000'
    ';' = '0056360000'; '<' = '0814224100'; '=' = '1414141414'
    '>' = '0041221408'; '?' = '0201510906'; '@' = '324979413E'
    'A' = '7E1111117E'; 'B' = '7F49494936'; 'C' = '3E41414122'
    'D' = '7F4141221C'; 'E' = '7F49494941'; 'F' = '7F09090901'
    'G' = '3E41495173'; 'H' = '7F0808087F'; 'I' = '00417F4100'
    'J' = '2040413F01'; 'K' = '7F08142241'; 'L' = '7F40404040'
    'M' = '7F020C027F'; 'N' = '7F0408107F'; 'O' = '3E4141413E'
    'P' = '7F09090906'; 'Q' = '3E4151215E'; 'R' = '7F09192946'
    'S' = '4649494931'; 'T' = '01017F0101'; 'U' = '3F4040403F'
    'V' = '1F2040201F'; 'W' = '3F4038403F'; 'X' = '6314081463'
    'Y' = '0708700807'; 'Z' = '6151494543'; '[' = '007F414100'
    '\' = '0204081020'; ']' = '0041417F00'; '^' = '0402010204'
    '_' = '4040404040'; '`' = '0001020400'; '{' = '0008364100'
    '|' = '00007F0000'; '}' = '0041360800'; '~' = '0804020408'
}

[byte[]]$bytes = [byte[]]::new(32 + 256 * 16)
function Set-U32([int]$Offset, [uint32]$Value) {
    for ($index = 0; $index -lt 4; ++$index) {
        $bytes[$Offset + $index] = ($Value -shr (8 * $index)) -band 0xff
    }
}
Set-U32 0 2253043058
Set-U32 4 0
Set-U32 8 32
Set-U32 12 0
Set-U32 16 256
Set-U32 20 16
Set-U32 24 16
Set-U32 28 8

for ($codepoint = 0; $codepoint -lt 256; ++$codepoint) {
    $character = [char]$codepoint
    $key = [string]$character
    if ($codepoint -ge 97 -and $codepoint -le 122) {
        $key = $key.ToUpperInvariant()
    }
    if (-not $patterns.ContainsKey($key)) { continue }
    $columns = $patterns[$key]
    for ($sourceRow = 0; $sourceRow -lt 7; ++$sourceRow) {
        [byte]$row = 0
        for ($column = 0; $column -lt 5; ++$column) {
            [byte]$columnBits = [Convert]::ToByte($columns.Substring($column * 2, 2), 16)
            if (($columnBits -band (1 -shl $sourceRow)) -ne 0) {
                $row = $row -bor (1 -shl (6 - $column))
            }
        }
        $offset = 32 + $codepoint * 16 + 1 + $sourceRow * 2
        $bytes[$offset] = $row
        $bytes[$offset + 1] = $row
    }
}

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
$lines.Add('/* Generated deterministically by tools/image/build_psf2_font.ps1. */')
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
