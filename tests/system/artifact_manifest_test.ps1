[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..\..'),
    [string]$ArtifactDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Set-U16([byte[]]$Bytes, [int]$Offset, [uint16]$Value) {
    $Bytes[$Offset] = $Value -band 0xff
    $Bytes[$Offset + 1] = ($Value -shr 8) -band 0xff
}
function Set-U32([byte[]]$Bytes, [int]$Offset, [uint32]$Value) {
    for ($index = 0; $index -lt 4; ++$index) { $Bytes[$Offset + $index] = ($Value -shr (8 * $index)) -band 0xff }
}
function Set-U64([byte[]]$Bytes, [int]$Offset, [uint64]$Value) {
    for ($index = 0; $index -lt 8; ++$index) { $Bytes[$Offset + $index] = ($Value -shr (8 * $index)) -band 0xff }
}
function New-StaticElf64([string]$Path, [byte]$Marker) {
    [byte[]]$bytes = [byte[]]::new(120)
    $bytes[0] = 0x7f; $bytes[1] = 0x45; $bytes[2] = 0x4c; $bytes[3] = 0x46
    $bytes[4] = 2; $bytes[5] = 1; $bytes[6] = 1
    Set-U16 $bytes 16 2; Set-U16 $bytes 18 0x3e; Set-U32 $bytes 20 1
    Set-U64 $bytes 32 64; Set-U16 $bytes 52 64; Set-U16 $bytes 54 56; Set-U16 $bytes 56 1
    Set-U32 $bytes 64 1
    $bytes[119] = $Marker
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}
function Assert-ZeroSamples([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        foreach ($sampleOffset in @(0, 512, 1GB, 49999999488, ($stream.Length - 512))) {
            [int64]$offset = $sampleOffset
            $stream.Position = $offset
            [byte[]]$sample = [byte[]]::new(512)
            if ($stream.Read($sample, 0, $sample.Length) -ne 512) { throw "Short read at $offset." }
            if (@($sample | Where-Object { $_ -ne 0 }).Count -ne 0) { throw "Nonzero sparse-disk data at $offset." }
        }
    } finally { $stream.Dispose() }
}

$root = [System.IO.Path]::GetFullPath($RepositoryRoot)
$moduleBuilder = Join-Path $root 'tools/image/build_system_modules.ps1'
$espBuilder = Join-Path $root 'tools/image/build_esp.ps1'
$diskBuilder = Join-Path $root 'tools/image/create_persistent_disk.ps1'
$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $root 'build/system/artifact-manifest'
} else { [System.IO.Path]::GetFullPath($ArtifactDirectory) }
$caseRoot = Join-Path $artifactRoot "run-$PID"
$inputs = Join-Path $caseRoot 'inputs'
[System.IO.Directory]::CreateDirectory($inputs) | Out-Null
$shell = Join-Path $inputs 'shell.elf'
$explorer = Join-Path $inputs 'explorer.elf'
New-StaticElf64 $shell 0x51
New-StaticElf64 $explorer 0xa7
$definitionPath = Join-Path $inputs 'modules.json'
$definition = [ordered]@{
    schema_version = 1
    modules = @(
        [ordered]@{ application_identity = 20; role = 'file_explorer'; required = $false; entry_abi_version = 1; source = 'explorer.elf'; esp_path = '/InferenceOS/System/explorer.elf' },
        [ordered]@{ application_identity = 10; role = 'shell'; required = $true; entry_abi_version = 1; source = 'shell.elf'; esp_path = '/InferenceOS/System/shell.elf' }
    )
}
[System.IO.File]::WriteAllText(
    $definitionPath, ($definition | ConvertTo-Json -Depth 5) + "`n",
    [System.Text.UTF8Encoding]::new($false)
)
$firstModules = Join-Path $caseRoot 'modules-a'
$secondModules = Join-Path $caseRoot 'modules-b'
& $moduleBuilder -DefinitionPath $definitionPath -OutputDirectory $firstModules
& $moduleBuilder -DefinitionPath $definitionPath -OutputDirectory $secondModules
$firstManifest = Join-Path $firstModules 'InferenceOS/System/modules.manifest'
$secondManifest = Join-Path $secondModules 'InferenceOS/System/modules.manifest'
if ((Get-FileHash $firstManifest -Algorithm SHA256).Hash -cne
    (Get-FileHash $secondManifest -Algorithm SHA256).Hash) {
    throw 'Repeated module packaging produced different manifests.'
}
$lines = @(Get-Content -LiteralPath $firstManifest)
if ($lines.Count -ne 3 -or $lines[0] -cne 'INFERENCEOS-SYSTEM-MODULES|1') {
    throw 'Module manifest header or record count is invalid.'
}
[uint64]$previousIdentity = 0
foreach ($line in $lines[1..2]) {
    $fields = $line.Split('|')
    if ($fields.Count -ne 7) { throw "Malformed module record '$line'." }
    [uint64]$identity = $fields[0]
    if ($identity -le $previousIdentity -or $fields[3] -cne '1' -or $fields[6] -notmatch '^/InferenceOS/System/[A-Za-z0-9._-]+$') {
        throw "Invalid sorted/versioned module record '$line'."
    }
    $previousIdentity = $identity
    $packaged = Join-Path $firstModules $fields[6].TrimStart('/').Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    if ([uint64](Get-Item $packaged).Length -ne [uint64]$fields[4] -or
        (Get-FileHash $packaged -Algorithm SHA256).Hash.ToLowerInvariant() -cne $fields[5]) {
        throw "Length or digest mismatch for '$packaged'."
    }
    $peer = Join-Path $secondModules $fields[6].TrimStart('/').Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    if ((Get-FileHash $packaged -Algorithm SHA256).Hash -cne (Get-FileHash $peer -Algorithm SHA256).Hash) {
        throw "Packaged module '$($fields[6])' is not reproducible."
    }
}

$loader = Join-Path $inputs 'loader.efi'
$kernel = Join-Path $inputs 'kernel.elf'
[System.IO.File]::WriteAllBytes($loader, [byte[]](1,3,3,7,9))
[System.IO.File]::WriteAllBytes($kernel, [byte[]](2,4,6,8,10,12))
$espA = Join-Path $caseRoot 'esp-a.img'
$espB = Join-Path $caseRoot 'esp-b.img'
& $espBuilder -LoaderPath $loader -KernelPath $kernel -ModuleDirectory $firstModules -OutputPath $espA -SizeMiB 33 -Force
& $espBuilder -LoaderPath $loader -KernelPath $kernel -ModuleDirectory $secondModules -OutputPath $espB -SizeMiB 33 -Force
$espDigest = (Get-FileHash $espA -Algorithm SHA256).Hash
if ($espDigest -cne (Get-FileHash $espB -Algorithm SHA256).Hash) {
    throw 'Repeated ESP packaging is not byte-for-byte reproducible.'
}
$espManifestA = "$espA.manifest.json"
$espManifestB = "$espB.manifest.json"
if (-not [System.IO.File]::Exists($espManifestA) -or -not [System.IO.File]::Exists($espManifestB) -or
    (Get-FileHash $espManifestA -Algorithm SHA256).Hash -cne
        (Get-FileHash $espManifestB -Algorithm SHA256).Hash) {
    throw 'Repeated ESP packaging did not produce identical deterministic manifests.'
}
$espImageManifest = Get-Content -Raw -LiteralPath $espManifestA | ConvertFrom-Json
if ($espImageManifest.schema_version -ne 1 -or $espImageManifest.artifact_kind -cne 'esp-image' -or
    $espImageManifest.content.model -cne 'file-sha256' -or
    $espImageManifest.content.digest -cne $espDigest.ToLowerInvariant()) {
    throw 'ESP image manifest does not identify the generated image.'
}

$minimumSize = [uint64]50000000000
$diskA = Join-Path $caseRoot 'disk-a.raw'
$diskB = Join-Path $caseRoot 'disk-b.raw'
& $diskBuilder -OutputPath $diskA -SizeBytes $minimumSize -Force
& $diskBuilder -OutputPath $diskB -SizeBytes $minimumSize -Force
if ([uint64](Get-Item $diskA).Length -ne $minimumSize -or [uint64](Get-Item $diskB).Length -ne $minimumSize) {
    throw 'Reference disks do not preserve the minimum logical capacity.'
}
Assert-ZeroSamples $diskA
Assert-ZeroSamples $diskB
$diskManifestA = "$diskA.manifest.json"
$diskManifestB = "$diskB.manifest.json"
if (-not [System.IO.File]::Exists($diskManifestA) -or -not [System.IO.File]::Exists($diskManifestB) -or
    (Get-FileHash $diskManifestA -Algorithm SHA256).Hash -cne
        (Get-FileHash $diskManifestB -Algorithm SHA256).Hash) {
    throw 'Repeated sparse-disk generation did not produce identical deterministic manifests.'
}
$diskImageManifest = Get-Content -Raw -LiteralPath $diskManifestA | ConvertFrom-Json
if ($diskImageManifest.schema_version -ne 1 -or
    $diskImageManifest.artifact_kind -cne 'persistent-disk' -or
    $diskImageManifest.content.model -cne 'zero-filled' -or
    [uint64]$diskImageManifest.content.logical_bytes -ne $minimumSize) {
    throw 'Sparse-disk manifest does not identify the generated logical image.'
}

$reproArtifact = Join-Path $caseRoot 'reproducibility-probe.bin'
$reproManifest = "$reproArtifact.manifest.json"
[System.IO.File]::WriteAllBytes($reproArtifact, [byte[]](1,2,3,4))
& (Join-Path $root 'tools/image/write_manifest.ps1') -ArtifactPath $reproArtifact `
    -ArtifactKind 'reproducibility-probe' -PropertiesJson '{"case":1}' | Out-Null
[System.IO.File]::WriteAllBytes($reproArtifact, [byte[]](4,3,2,1))
$reproducibilityFailureObserved = $false
try {
    & (Join-Path $root 'tools/image/write_manifest.ps1') -ArtifactPath $reproArtifact `
        -ArtifactKind 'reproducibility-probe' -PropertiesJson '{"case":1}' | Out-Null
} catch {
    $reproducibilityFailureObserved = $_.Exception.Message.Contains('Reproducibility check failed')
}
if (-not $reproducibilityFailureObserved) {
    throw 'The manifest writer accepted different bytes produced from the same recipe.'
}

[pscustomobject]@{
    Passed = $true
    ModuleManifestDigest = (Get-FileHash $firstManifest -Algorithm SHA256).Hash
    EspDigest = $espDigest
    DiskLogicalSize = $minimumSize
    ArtifactDirectory = $caseRoot
}
