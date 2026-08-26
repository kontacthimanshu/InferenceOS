[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DefinitionPath,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ManifestHeader = "INFERENCEOS-SYSTEM-MODULES|1"
$KnownRoles = [ordered]@{
    shell = 1
    gui_desktop = 2
    gui_terminal = 3
    file_explorer = 4
    test_application = 5
}

function Read-UInt16LE([byte[]]$Bytes, [int]$Offset) {
    return [uint16]($Bytes[$Offset] -bor ($Bytes[$Offset + 1] -shl 8))
}

function Read-UInt64LE([byte[]]$Bytes, [int]$Offset) {
    [uint64]$value = 0
    for ($index = 0; $index -lt 8; ++$index) {
        $value = $value -bor ([uint64]$Bytes[$Offset + $index] -shl (8 * $index))
    }
    return $value
}

function Read-UInt32LE([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Assert-StaticElf64([string]$Path) {
    [byte[]]$header = [System.IO.File]::ReadAllBytes($Path)
    if ($header.Length -lt 64 -or $header[0] -ne 0x7f -or $header[1] -ne 0x45 -or
        $header[2] -ne 0x4c -or $header[3] -ne 0x46 -or $header[4] -ne 2 -or
        $header[5] -ne 1 -or (Read-UInt16LE $header 16) -ne 2 -or
        (Read-UInt16LE $header 18) -ne 0x3e) {
        throw "Module '$Path' is not a little-endian, static x86-64 ELF64 executable."
    }
    [uint64]$programOffset = Read-UInt64LE $header 32
    [uint16]$entrySize = Read-UInt16LE $header 54
    [uint16]$entryCount = Read-UInt16LE $header 56
    if ($entryCount -eq 0 -or $entrySize -lt 56 -or
        $programOffset -gt [uint64]$header.Length -or
        [uint64]$entryCount * $entrySize -gt [uint64]$header.Length - $programOffset) {
        throw "Module '$Path' has an invalid ELF64 program-header table."
    }
    for ([uint64]$index = 0; $index -lt $entryCount; ++$index) {
        [uint64]$offset = $programOffset + $index * $entrySize
        [uint32]$type = [BitConverter]::ToUInt32($header, [int]$offset)
        if ($type -eq 3) {
            throw "Module '$Path' contains PT_INTERP and is not statically linked."
        }
    }
}

function Assert-Psf2Font([string]$Path) {
    [byte[]]$font = [System.IO.File]::ReadAllBytes($Path)
    if ($font.Length -lt 32 -or (Read-UInt32LE $font 0) -ne 2253043058 -or
        (Read-UInt32LE $font 4) -ne 0 -or (Read-UInt32LE $font 8) -ne 32 -or
        (Read-UInt32LE $font 12) -ne 0 -or (Read-UInt32LE $font 16) -ne 256 -or
        (Read-UInt32LE $font 20) -ne 16 -or (Read-UInt32LE $font 24) -ne 16 -or
        (Read-UInt32LE $font 28) -ne 8 -or $font.Length -ne 4128) {
        throw "Asset '$Path' is not the required Unicode-free 256-glyph 8x16 PSF2 font."
    }
}

function Get-NormalizedEspPath([string]$Path) {
    $normalized = $Path.Replace('\', '/')
    if (-not $normalized.StartsWith('/') -or $normalized.Contains('//') -or
        $normalized.Contains('/./') -or $normalized.Contains('/../') -or
        $normalized.EndsWith('/') -or $normalized.Length -gt 255 -or
        $normalized -notmatch '^/InferenceOS/System/[A-Za-z0-9._-]+$') {
        throw "Invalid ESP path '$Path'. Use /InferenceOS/System/<file>."
    }
    return $normalized
}

$definition = (Get-Content -Raw -LiteralPath $DefinitionPath | ConvertFrom-Json)
if ($definition.schema_version -ne 1 -or $null -eq $definition.modules) {
    throw "Module definition must use schema_version 1 and contain a modules array."
}
$modules = @($definition.modules)
if ($modules.Count -eq 0 -or $modules.Count -gt 32) {
    throw "Module definition must contain between 1 and 32 modules."
}

$definitionRoot = Split-Path -Parent ([System.IO.Path]::GetFullPath($DefinitionPath))
$seenIdentity = @{}
$seenRole = @{}
$seenPath = @{}
$records = [System.Collections.Generic.List[object]]::new()
$assetRecords = [System.Collections.Generic.List[object]]::new()
$shellCount = 0

foreach ($module in $modules) {
    [uint64]$identity = $module.application_identity
    $roleName = ([string]$module.role).ToLowerInvariant()
    if ($identity -eq 0 -or -not $KnownRoles.Contains($roleName)) {
        throw "Each module needs a nonzero identity and a known role."
    }
    if ($seenIdentity.ContainsKey($identity)) {
        throw "Duplicate application identity '$identity'."
    }
    if ($roleName -ne 'test_application' -and $seenRole.ContainsKey($roleName)) {
        throw "Duplicate singleton role '$roleName'."
    }
    [bool]$required = $module.required
    if ($roleName -eq 'shell') {
        ++$shellCount
        if (-not $required) { throw "The Shell module must be required." }
    }
    [uint32]$abi = if ($null -eq $module.PSObject.Properties['entry_abi_version']) {
        1
    } else {
        $module.entry_abi_version
    }
    if ($abi -ne 1) { throw "Module '$identity' uses unsupported entry ABI '$abi'." }
    $source = [System.IO.Path]::GetFullPath((Join-Path $definitionRoot ([string]$module.source)))
    if (-not [System.IO.File]::Exists($source)) { throw "Module source '$source' does not exist." }
    Assert-StaticElf64 $source
    $espPath = Get-NormalizedEspPath ([string]$module.esp_path)
    $pathKey = $espPath.ToUpperInvariant()
    if ($seenPath.ContainsKey($pathKey)) { throw "Duplicate ESP path '$espPath'." }
    $seenIdentity[$identity] = $true
    $seenRole[$roleName] = $true
    $seenPath[$pathKey] = $true
    $file = Get-Item -LiteralPath $source
    $records.Add([pscustomobject]@{
        Identity = $identity; Role = [uint32]$KnownRoles[$roleName]; Required = $required
        Abi = $abi; Length = [uint64]$file.Length
        Digest = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        EspPath = $espPath; Source = $source
    })
}
if ($shellCount -ne 1) { throw "Exactly one required Shell module is mandatory." }

$assets = @()
if ($null -ne $definition.PSObject.Properties['assets']) {
    $assets = @($definition.assets)
}
if ($assets.Count -gt 8) { throw 'Module definition cannot contain more than 8 assets.' }
foreach ($asset in $assets) {
    $kind = ([string]$asset.kind).ToLowerInvariant()
    if ($kind -ne 'psf2_font') { throw "Unknown system asset kind '$kind'." }
    if ([string]$asset.license -cne 'MIT') {
        throw "The PSF2 font asset must carry its explicit MIT license identifier."
    }
    $source = [System.IO.Path]::GetFullPath((Join-Path $definitionRoot ([string]$asset.source)))
    if (-not [System.IO.File]::Exists($source)) { throw "Asset source '$source' does not exist." }
    Assert-Psf2Font $source
    $espPath = Get-NormalizedEspPath ([string]$asset.esp_path)
    $pathKey = $espPath.ToUpperInvariant()
    if ($seenPath.ContainsKey($pathKey)) { throw "Duplicate ESP path '$espPath'." }
    $seenPath[$pathKey] = $true
    $file = Get-Item -LiteralPath $source
    $assetRecords.Add([pscustomobject]@{
        Kind = $kind; Length = [uint64]$file.Length
        Digest = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        EspPath = $espPath; Source = $source
    })
}

$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
if ($outputRoot -eq [System.IO.Path]::GetPathRoot($outputRoot) -or $outputRoot -eq $definitionRoot) {
    throw "Output directory '$outputRoot' is too broad or overlaps the definition directory."
}
foreach ($record in @($records) + @($assetRecords)) {
    $outputPrefix = $outputRoot.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    if ($record.Source.StartsWith($outputPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Module source '$($record.Source)' must not be inside the replaceable output directory."
    }
}
if ([System.IO.Directory]::Exists($outputRoot)) {
    if (-not $Force) { throw "Output directory '$outputRoot' exists; pass -Force to replace it." }
    [System.IO.Directory]::Delete($outputRoot, $true)
}
[System.IO.Directory]::CreateDirectory($outputRoot) | Out-Null

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add($ManifestHeader)
foreach ($record in ($records | Sort-Object Identity)) {
    $relative = $record.EspPath.TrimStart('/').Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $destination = Join-Path $outputRoot $relative
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
    [System.IO.File]::Copy($record.Source, $destination, $false)
    $requiredValue = if ($record.Required) { 1 } else { 0 }
    $lines.Add("$($record.Identity)|$($record.Role)|$requiredValue|$($record.Abi)|$($record.Length)|$($record.Digest)|$($record.EspPath)")
}
$manifestPath = Join-Path $outputRoot "InferenceOS/System/modules.manifest"
$manifestText = ($lines -join "`n") + "`n"
[System.IO.File]::WriteAllText($manifestPath, $manifestText, [System.Text.UTF8Encoding]::new($false))

foreach ($record in ($assetRecords | Sort-Object EspPath)) {
    $relative = $record.EspPath.TrimStart('/').Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $destination = Join-Path $outputRoot $relative
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
    [System.IO.File]::Copy($record.Source, $destination, $false)
}

$hashLines = [System.Collections.Generic.List[string]]::new()
$hashLines.Add('INFERENCEOS-SYSTEM-HASHES|1')
$packagedRecords = @($records) + @($assetRecords)
foreach ($record in ($packagedRecords | Sort-Object EspPath)) {
    $hashLines.Add("$($record.Digest)|$($record.Length)|$($record.EspPath)")
}
$manifestFile = Get-Item -LiteralPath $manifestPath
$manifestDigest = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$hashLines.Add("$manifestDigest|$([uint64]$manifestFile.Length)|/InferenceOS/System/modules.manifest")
$hashPath = Join-Path $outputRoot 'InferenceOS/System/modules.sha256'
[System.IO.File]::WriteAllText(
    $hashPath, ($hashLines -join "`n") + "`n", [System.Text.UTF8Encoding]::new($false)
)
Write-Output $manifestPath
