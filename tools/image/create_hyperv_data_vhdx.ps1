[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputPath,
    [uint64]$SizeBytes = 64GB,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw 'Hyper-V VHDX generation must run in Windows PowerShell.'
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Hyper-V VHDX generation requires an elevated Windows PowerShell session.'
}
if (-not (Get-Command New-VHD -ErrorAction SilentlyContinue)) {
    throw 'New-VHD is unavailable. Run elevated PowerShell on the Hyper-V host.'
}
if ($SizeBytes -lt 50000000000 -or ($SizeBytes % 512) -ne 0) {
    throw 'The InferenceOS-FS data disk must be at least 50,000,000,000 bytes and 512-byte aligned.'
}
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
[System.IO.Directory]::CreateDirectory((Split-Path -Parent $output)) | Out-Null
$preservedAcl = $null
if (Test-Path -LiteralPath $output) {
    if (-not $Force) { throw "Data VHDX '$output' exists; pass -Force to replace it." }
    $preservedAcl = Get-Acl -LiteralPath $output
    Remove-Item -LiteralPath $output -Force
    Remove-Item -LiteralPath "$output.manifest.json" -Force -ErrorAction SilentlyContinue
}

# Deliberately do not mount, initialize, partition, or host-format this disk.
# InferenceOS receives a zero-filled whole disk and creates InferenceOS-FS itself.
New-VHD -Path $output -SizeBytes $SizeBytes -Dynamic -BlockSizeBytes 2MB `
    -LogicalSectorSizeBytes 512 -PhysicalSectorSizeBytes 4096 | Out-Null
if ($null -ne $preservedAcl) {
    Set-Acl -LiteralPath $output -AclObject $preservedAcl
}

$writer = Join-Path $PSScriptRoot 'write_manifest.ps1'
$properties = [ordered]@{
    virtual_size_bytes = $SizeBytes
    logical_sector_size = 512
    initial_state = 'unpartitioned-zero-filled'
    guest_filesystem = 'InferenceOS-FS'
}
& $writer -ArtifactPath $output -ArtifactKind 'hyperv-data-vhdx' -ContentModel file-sha256 `
    -PropertiesJson ($properties | ConvertTo-Json -Compress) | Out-Null
Write-Output $output
