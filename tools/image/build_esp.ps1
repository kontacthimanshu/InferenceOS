[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$LoaderPath,
    [Parameter(Mandatory)][string]$KernelPath,
    [Parameter(Mandatory)][string]$ModuleDirectory,
    [Parameter(Mandatory)][string]$OutputPath,
    [ValidateRange(33, 2048)][uint32]$SizeMiB = 64,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$SectorSize = 512
$SectorsPerCluster = 1
$ReservedSectors = 32
$FatCount = 2
$EndOfChain = [uint32]0x0fffffff
$manifestWriter = Join-Path $PSScriptRoot 'write_manifest.ps1'

function Set-U16([byte[]]$Bytes, [int]$Offset, [uint16]$Value) {
    $Bytes[$Offset] = $Value -band 0xff
    $Bytes[$Offset + 1] = ($Value -shr 8) -band 0xff
}
function Set-U32([byte[]]$Bytes, [int]$Offset, [uint32]$Value) {
    for ($index = 0; $index -lt 4; ++$index) {
        $Bytes[$Offset + $index] = ($Value -shr (8 * $index)) -band 0xff
    }
}
function Set-FatEntry([byte[]]$Fat, [uint32]$Cluster, [uint32]$Value) {
    Set-U32 $Fat ([int]$Cluster * 4) $Value
}
function Get-ShortName([string]$Name, [hashtable]$Used) {
    $lastDot = $Name.LastIndexOf('.')
    $base = if ($lastDot -gt 0) { $Name.Substring(0, $lastDot) } else { $Name }
    $extension = if ($lastDot -gt 0) { $Name.Substring($lastDot + 1) } else { '' }
    $baseClean = ($base.ToUpperInvariant() -replace '[^A-Z0-9_$%''\-@~`!(){}^#&]', '')
    $extensionClean = ($extension.ToUpperInvariant() -replace '[^A-Z0-9_$%''\-@~`!(){}^#&]', '')
    $simple = $baseClean -eq $base.ToUpperInvariant() -and
        $extensionClean -eq $extension.ToUpperInvariant() -and
        $baseClean.Length -ge 1 -and $baseClean.Length -le 8 -and $extensionClean.Length -le 3
    if ($simple) {
        $candidate = $baseClean.PadRight(8) + $extensionClean.PadRight(3)
        if (-not $Used.ContainsKey($candidate)) { $Used[$candidate] = $true; return $candidate }
    }
    if ($baseClean.Length -eq 0) { $baseClean = 'FILE' }
    for ($number = 1; $number -le 999999; ++$number) {
        $tail = "~$number"
        $prefixLength = 8 - $tail.Length
        $prefix = $baseClean.Substring(0, [Math]::Min($baseClean.Length, $prefixLength))
        $candidate = ($prefix + $tail).PadRight(8) +
            $extensionClean.Substring(0, [Math]::Min(3, $extensionClean.Length)).PadRight(3)
        if (-not $Used.ContainsKey($candidate)) { $Used[$candidate] = $true; return $candidate }
    }
    throw "Unable to create a unique FAT short name for '$Name'."
}
function Get-ShortChecksum([string]$ShortName) {
    [byte]$sum = 0
    foreach ($value in [System.Text.Encoding]::ASCII.GetBytes($ShortName)) {
        $sum = [byte](((([int]$sum -band 1) -shl 7) + ([int]$sum -shr 1) + $value) -band 0xff)
    }
    return $sum
}
function Test-NeedsLongName([string]$Name, [string]$ShortName) {
    $rendered = $ShortName.Substring(0, 8).TrimEnd()
    $extension = $ShortName.Substring(8, 3).TrimEnd()
    if ($extension) { $rendered += ".$extension" }
    return $Name -cne $rendered
}
function New-Node([string]$Name, [bool]$Directory, [string]$Source = '') {
    return [pscustomobject]@{
        Name = $Name; Directory = $Directory; Source = $Source
        Children = [System.Collections.Generic.List[object]]::new()
        ShortName = ''; FirstCluster = [uint32]0; ClusterCount = [uint32]0
        ParentCluster = [uint32]0; Length = [uint32]0
    }
}
function Add-Child($Parent, $Child) { $Parent.Children.Add($Child); return $Child }
function Assign-Names($Directory) {
    $used = @{}
    foreach ($child in $Directory.Children) {
        $child.ShortName = Get-ShortName $child.Name $used
        if ($child.Directory) { Assign-Names $child }
    }
}
function Get-DirectoryEntryCount($Directory, [bool]$Root) {
    $count = if ($Root) { 1 } else { 3 }
    foreach ($child in $Directory.Children) {
        if (Test-NeedsLongName $child.Name $child.ShortName) {
            $count += [Math]::Ceiling(($child.Name.Length + 1) / 13.0)
        }
        ++$count
    }
    return $count
}
function Add-LfnEntries([System.Collections.Generic.List[byte]]$Output, [string]$Name, [string]$ShortName) {
    [System.Collections.Generic.List[uint16]]$characters = [System.Collections.Generic.List[uint16]]::new()
    foreach ($character in $Name.ToCharArray()) { $characters.Add([uint16]$character) }
    $characters.Add(0)
    while (($characters.Count % 13) -ne 0) { $characters.Add(0xffff) }
    $parts = $characters.Count / 13
    $positions = @(1,3,5,7,9,14,16,18,20,22,24,28,30)
    $checksum = Get-ShortChecksum $ShortName
    for ($part = $parts; $part -ge 1; --$part) {
        [byte[]]$entry = [byte[]]::new(32)
        $entry[0] = [byte]($part -bor $(if ($part -eq $parts) { 0x40 } else { 0 }))
        $entry[11] = 0x0f; $entry[12] = 0; $entry[13] = $checksum
        for ($index = 0; $index -lt 13; ++$index) {
            Set-U16 $entry $positions[$index] $characters[(($part - 1) * 13) + $index]
        }
        $Output.AddRange($entry)
    }
}
function Add-ShortEntry(
    [System.Collections.Generic.List[byte]]$Output,
    [string]$ShortName,
    [byte]$Attributes,
    [uint32]$FirstCluster,
    [uint32]$Length
) {
    [byte[]]$entry = [byte[]]::new(32)
    [System.Text.Encoding]::ASCII.GetBytes($ShortName).CopyTo($entry, 0)
    $entry[11] = $Attributes
    Set-U16 $entry 20 ([uint16]($FirstCluster -shr 16))
    Set-U16 $entry 24 0x0021
    Set-U16 $entry 26 ([uint16]($FirstCluster -band 0xffff))
    Set-U32 $entry 28 $Length
    $Output.AddRange($entry)
}
function Get-DirectoryBytes($Directory, [bool]$Root) {
    [System.Collections.Generic.List[byte]]$bytes = [System.Collections.Generic.List[byte]]::new()
    if (-not $Root) {
        Add-ShortEntry $bytes '.          ' 0x10 $Directory.FirstCluster 0
        Add-ShortEntry $bytes '..         ' 0x10 $Directory.ParentCluster 0
    }
    foreach ($child in $Directory.Children) {
        if (Test-NeedsLongName $child.Name $child.ShortName) {
            Add-LfnEntries $bytes $child.Name $child.ShortName
        }
        Add-ShortEntry $bytes $child.ShortName $(if ($child.Directory) { 0x10 } else { 0x20 }) `
            $child.FirstCluster $(if ($child.Directory) { 0 } else { $child.Length })
    }
    $bytes.Add(0)
    return $bytes.ToArray()
}

foreach ($required in @($LoaderPath, $KernelPath)) {
    if (-not [System.IO.File]::Exists($required)) { throw "Required artifact '$required' does not exist." }
    if ((Get-Item -LiteralPath $required).Length -eq 0) { throw "Required artifact '$required' is empty." }
}
$moduleRoot = [System.IO.Path]::GetFullPath($ModuleDirectory)
$manifestSource = Join-Path $moduleRoot 'InferenceOS/System/modules.manifest'
if (-not [System.IO.File]::Exists($manifestSource)) {
    throw "Module directory does not contain InferenceOS/System/modules.manifest."
}

$root = New-Node '' $true
$efi = Add-Child $root (New-Node 'EFI' $true)
$boot = Add-Child $efi (New-Node 'BOOT' $true)
[void](Add-Child $boot (New-Node 'BOOTX64.EFI' $false ([System.IO.Path]::GetFullPath($LoaderPath))))
$inference = Add-Child $root (New-Node 'InferenceOS' $true)
$kernelDirectory = Add-Child $inference (New-Node 'Kernel' $true)
[void](Add-Child $kernelDirectory (New-Node 'kernel.elf' $false ([System.IO.Path]::GetFullPath($KernelPath))))
$system = Add-Child $inference (New-Node 'System' $true)
Get-ChildItem -LiteralPath (Split-Path -Parent $manifestSource) -File | Sort-Object Name | ForEach-Object {
    [void](Add-Child $system (New-Node $_.Name $false $_.FullName))
}
Assign-Names $root

[uint64]$totalBytes = [uint64]$SizeMiB * 1MB
[uint32]$totalSectors = $totalBytes / $SectorSize
[uint32]$fatSectors = [Math]::Ceiling(
    (($totalSectors - $ReservedSectors) * 4.0) /
    (($SectorSize * $SectorsPerCluster) + ($FatCount * 4))
)
[uint32]$dataSectors = $totalSectors - $ReservedSectors - $FatCount * $fatSectors
[uint32]$availableClusters = [Math]::Floor($dataSectors / $SectorsPerCluster)
[uint32]$nextCluster = 2
$directories = [System.Collections.Generic.List[object]]::new()
$files = [System.Collections.Generic.List[object]]::new()
function Allocate-Tree($Directory, [uint32]$ParentCluster, [bool]$Root) {
    $Directory.ParentCluster = $ParentCluster
    $entryCount = Get-DirectoryEntryCount $Directory $Root
    $Directory.ClusterCount = [Math]::Ceiling(($entryCount * 32) / ($SectorSize * $SectorsPerCluster))
    $Directory.FirstCluster = $script:nextCluster
    $script:nextCluster += $Directory.ClusterCount
    $script:directories.Add($Directory)
    foreach ($child in $Directory.Children) {
        if ($child.Directory) {
            Allocate-Tree $child $Directory.FirstCluster $false
        } else {
            [uint64]$length = (Get-Item -LiteralPath $child.Source).Length
            if ($length -gt [uint32]::MaxValue) { throw "ESP file '$($child.Source)' exceeds FAT32 file size." }
            $child.Length = [uint32]$length
            $child.ClusterCount = [Math]::Ceiling($length / ($SectorSize * $SectorsPerCluster))
            $child.FirstCluster = if ($child.ClusterCount -eq 0) { 0 } else { $script:nextCluster }
            $script:nextCluster += $child.ClusterCount
            $script:files.Add($child)
        }
    }
}
Allocate-Tree $root 0 $true
if ($nextCluster -gt $availableClusters + 2) { throw "ESP contents exceed the requested image size." }

[byte[]]$fat = [byte[]]::new($fatSectors * $SectorSize)
Set-FatEntry $fat 0 0x0ffffff8
Set-FatEntry $fat 1 $EndOfChain
foreach ($node in @($directories) + @($files)) {
    for ([uint32]$index = 0; $index -lt $node.ClusterCount; ++$index) {
        $cluster = $node.FirstCluster + $index
        Set-FatEntry $fat $cluster $(if ($index + 1 -eq $node.ClusterCount) { $EndOfChain } else { $cluster + 1 })
    }
}

[byte[]]$bootSector = [byte[]]::new($SectorSize)
$bootSector[0]=0xeb; $bootSector[1]=0x58; $bootSector[2]=0x90
[System.Text.Encoding]::ASCII.GetBytes('INFEROS ').CopyTo($bootSector,3)
Set-U16 $bootSector 11 $SectorSize; $bootSector[13]=$SectorsPerCluster
Set-U16 $bootSector 14 $ReservedSectors; $bootSector[16]=$FatCount
Set-U16 $bootSector 17 0; Set-U16 $bootSector 19 0; $bootSector[21]=0xf8
Set-U16 $bootSector 22 0; Set-U16 $bootSector 24 63; Set-U16 $bootSector 26 255
Set-U32 $bootSector 28 0; Set-U32 $bootSector 32 $totalSectors
Set-U32 $bootSector 36 $fatSectors; Set-U16 $bootSector 40 0; Set-U16 $bootSector 42 0
Set-U32 $bootSector 44 $root.FirstCluster; Set-U16 $bootSector 48 1; Set-U16 $bootSector 50 6
$bootSector[64]=0x80; $bootSector[66]=0x29; Set-U32 $bootSector 67 0x534f494e
[System.Text.Encoding]::ASCII.GetBytes('INFERENCEOS').CopyTo($bootSector,71)
[System.Text.Encoding]::ASCII.GetBytes('FAT32   ').CopyTo($bootSector,82)
$bootSector[510]=0x55; $bootSector[511]=0xaa
[byte[]]$fsInfo = [byte[]]::new($SectorSize)
Set-U32 $fsInfo 0 0x41615252; Set-U32 $fsInfo 484 0x61417272
Set-U32 $fsInfo 488 ($availableClusters - ($nextCluster - 2)); Set-U32 $fsInfo 492 $nextCluster
Set-U32 $fsInfo 508 ([uint32]2857697280)

$output = [System.IO.Path]::GetFullPath($OutputPath)
[System.IO.Directory]::CreateDirectory((Split-Path -Parent $output)) | Out-Null
if ([System.IO.File]::Exists($output)) {
    if (-not $Force) { throw "ESP '$output' exists; pass -Force to replace it." }
    [System.IO.File]::Delete($output)
}
$stream = [System.IO.File]::Open($output, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::ReadWrite)
try {
    $stream.SetLength([int64]$totalBytes)
    $stream.Position=0; $stream.Write($bootSector, 0, $bootSector.Length)
    $stream.Position=$SectorSize; $stream.Write($fsInfo, 0, $fsInfo.Length)
    $stream.Position=6*$SectorSize; $stream.Write($bootSector, 0, $bootSector.Length)
    $stream.Position=7*$SectorSize; $stream.Write($fsInfo, 0, $fsInfo.Length)
    for ($copy=0; $copy -lt $FatCount; ++$copy) {
        $stream.Position=($ReservedSectors + $copy*$fatSectors)*$SectorSize
        $stream.Write($fat, 0, $fat.Length)
    }
    [uint64]$dataOffset = ($ReservedSectors + $FatCount*$fatSectors)*$SectorSize
    foreach ($directory in $directories) {
        $bytes = Get-DirectoryBytes $directory ($directory -eq $root)
        $stream.Position = $dataOffset + ([uint64]$directory.FirstCluster - 2)*$SectorSize*$SectorsPerCluster
        $stream.Write($bytes, 0, $bytes.Length)
    }
    foreach ($file in $files) {
        $stream.Position = $dataOffset + ([uint64]$file.FirstCluster - 2)*$SectorSize*$SectorsPerCluster
        $input = [System.IO.File]::OpenRead($file.Source)
        try { $input.CopyTo($stream) } finally { $input.Dispose() }
    }
} finally { $stream.Dispose() }
$inputSpecs = @(
    "loader=$([System.IO.Path]::GetFullPath($LoaderPath))",
    "kernel=$([System.IO.Path]::GetFullPath($KernelPath))"
)
foreach ($file in Get-ChildItem -LiteralPath (Split-Path -Parent $manifestSource) -File | Sort-Object Name) {
    $inputSpecs += "system.$($file.Name.ToLowerInvariant())=$($file.FullName)"
}
$properties = [ordered]@{
    size_mib = $SizeMiB
    sector_size = $SectorSize
    sectors_per_cluster = $SectorsPerCluster
    reserved_sectors = $ReservedSectors
    fat_count = $FatCount
    volume_id = '534f494e'
}
& $manifestWriter -ArtifactPath $output -ArtifactKind 'esp-image' -ContentModel file-sha256 `
    -InputSpec $inputSpecs -PropertiesJson ($properties | ConvertTo-Json -Compress) | Out-Null
Write-Output $output
