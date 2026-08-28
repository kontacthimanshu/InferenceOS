[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$LoaderPath,
    [Parameter(Mandatory)][string]$KernelPath,
    [Parameter(Mandatory)][string]$ModuleDirectory,
    [Parameter(Mandatory)][string]$OutputPath,
    [ValidateRange(128MB, 2GB)][uint64]$SizeBytes = 256MB,
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
foreach ($cmdlet in 'New-VHD','Mount-VHD','Dismount-VHD','Initialize-Disk','New-Partition','Format-Volume') {
    if (-not (Get-Command $cmdlet -ErrorAction SilentlyContinue)) {
        throw "Required Hyper-V/storage cmdlet '$cmdlet' is unavailable. Run elevated PowerShell on the Hyper-V host."
    }
}
foreach ($required in $LoaderPath,$KernelPath) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing artifact '$required'." }
}
if (-not (Test-Path -LiteralPath $ModuleDirectory -PathType Container)) {
    throw "Missing module directory '$ModuleDirectory'."
}
$LoaderPath = (Resolve-Path -LiteralPath $LoaderPath).ProviderPath
$KernelPath = (Resolve-Path -LiteralPath $KernelPath).ProviderPath
$moduleRoot = (Resolve-Path -LiteralPath $ModuleDirectory).ProviderPath
$manifest = Join-Path $moduleRoot 'InferenceOS/System/modules.manifest'
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "Module tree '$moduleRoot' is incomplete."
}

$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
[System.IO.Directory]::CreateDirectory((Split-Path -Parent $output)) | Out-Null
$preservedAcl = $null
if (Test-Path -LiteralPath $output) {
    if (-not $Force) { throw "Boot VHDX '$output' exists; pass -Force to replace it." }
    $preservedAcl = Get-Acl -LiteralPath $output
    Remove-Item -LiteralPath $output -Force
    Remove-Item -LiteralPath "$output.manifest.json" -Force -ErrorAction SilentlyContinue
}

$mounted = $false
try {
    New-VHD -Path $output -SizeBytes $SizeBytes -Dynamic -BlockSizeBytes 1MB `
        -LogicalSectorSizeBytes 512 -PhysicalSectorSizeBytes 4096 | Out-Null
    $disk = Mount-VHD -Path $output -Passthru | Get-Disk
    $mounted = $true
    $disk = $disk | Initialize-Disk -PartitionStyle GPT -PassThru
    $partition = $disk | New-Partition -UseMaximumSize `
        -GptType '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}' -AssignDriveLetter
    $volume = $partition | Format-Volume -FileSystem FAT32 -NewFileSystemLabel 'INFERENCEOS' `
        -Confirm:$false -Force
    $root = "$($volume.DriveLetter):\"
    New-Item -ItemType Directory -Force (Join-Path $root 'EFI/BOOT') | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $root 'InferenceOS/Kernel') | Out-Null
    Copy-Item -LiteralPath $LoaderPath -Destination (Join-Path $root 'EFI/BOOT/BOOTX64.EFI')
    Copy-Item -LiteralPath $KernelPath -Destination (Join-Path $root 'InferenceOS/Kernel/kernel.elf')
    Copy-Item -LiteralPath (Join-Path $moduleRoot 'InferenceOS/System') `
        -Destination (Join-Path $root 'InferenceOS') -Recurse
    $sourceKernelHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $KernelPath).Hash
    $vhdxKernelHash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath (Join-Path $root 'InferenceOS/Kernel/kernel.elf')).Hash
    if ($sourceKernelHash -ne $vhdxKernelHash) {
        throw 'The kernel copied to the boot VHDX failed SHA-256 verification.'
    }
} finally {
    if ($mounted) { Dismount-VHD -Path $output -ErrorAction Continue }
}
if ($null -ne $preservedAcl) {
    Set-Acl -LiteralPath $output -AclObject $preservedAcl
}

$writer = Join-Path $PSScriptRoot 'write_manifest.ps1'
$properties = [ordered]@{
    virtual_size_bytes = $SizeBytes
    disk_layout = 'gpt'
    boot_partition = 'efi-system-partition'
    filesystem = 'fat32'
    logical_sector_size = 512
}
& $writer -ArtifactPath $output -ArtifactKind 'hyperv-boot-vhdx' -ContentModel file-sha256 `
    -InputSpec @("loader=$LoaderPath","kernel=$KernelPath","modules=$manifest") `
    -PropertiesJson ($properties | ConvertTo-Json -Compress) | Out-Null
Write-Output $output
