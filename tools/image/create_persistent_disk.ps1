[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputPath,
    [uint64]$SizeBytes = 64GB,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$MinimumSize = [uint64]50000000000
$SectorSize = [uint64]512
$manifestWriter = Join-Path $PSScriptRoot 'write_manifest.ps1'
if ($SizeBytes -lt $MinimumSize) {
    throw "Persistent disk must be at least $MinimumSize bytes."
}
if (($SizeBytes % $SectorSize) -ne 0) {
    throw "Persistent disk size must be a multiple of $SectorSize bytes."
}

$path = [System.IO.Path]::GetFullPath($OutputPath)
[System.IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
if ([System.IO.File]::Exists($path)) {
    $existingLength = (Get-Item -LiteralPath $path).Length
    if (-not $Force -and [uint64]$existingLength -eq $SizeBytes) {
        $existingManifestPath = "$path.manifest.json"
        if (-not [System.IO.File]::Exists($existingManifestPath)) {
            throw "Disk '$path' has no generation manifest; pass -Force to recreate and certify it."
        }
        try { $existingManifest = Get-Content -Raw -LiteralPath $existingManifestPath | ConvertFrom-Json } catch {
            throw "Disk manifest '$existingManifestPath' is invalid; pass -Force to recreate it."
        }
        if ($existingManifest.schema_version -ne 1 -or
            $existingManifest.artifact_kind -cne 'persistent-disk' -or
            $existingManifest.content.model -cne 'zero-filled' -or
            [uint64]$existingManifest.content.logical_bytes -ne $SizeBytes) {
            throw "Disk manifest '$existingManifestPath' does not describe the requested zero-filled disk; pass -Force."
        }
        Write-Output $path
        exit 0
    }
    if (-not $Force) {
        throw "Disk '$path' already exists with length $existingLength; pass -Force to replace it."
    }
    [System.IO.File]::Delete($path)
}

$runningOnWindows = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
if ($runningOnWindows -and (Get-Command fsutil.exe -ErrorAction SilentlyContinue)) {
    [System.IO.File]::WriteAllBytes($path, [byte[]]::new(0))
    & fsutil.exe sparse setflag $path | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Unable to mark '$path' sparse." }
}
$stream = [System.IO.File]::Open($path, [System.IO.FileMode]::OpenOrCreate,
    [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
try { $stream.SetLength([int64]$SizeBytes) } finally { $stream.Dispose() }
if ([uint64](Get-Item -LiteralPath $path).Length -ne $SizeBytes) {
    throw "Persistent disk logical-size verification failed."
}
$properties = [ordered]@{ logical_bytes = $SizeBytes; sector_size = $SectorSize; initial_state = 'zero-filled' }
& $manifestWriter -ArtifactPath $path -ArtifactKind 'persistent-disk' -ContentModel zero-filled `
    -PropertiesJson ($properties | ConvertTo-Json -Compress) | Out-Null
Write-Output $path
