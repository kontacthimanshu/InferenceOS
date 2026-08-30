[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$VmName = 'InferenceOS-HyperV',
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '../..'),
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$BuildPreset = 'gcc-debug',
    [string]$WslToolEnvironmentFile,
    [ValidateRange(512MB, 16GB)][uint64]$MemoryStartupBytes = 1GB,
    [ValidateRange(50000000000, [uint64]::MaxValue)][uint64]$DataDiskSizeBytes = 64GB,
    [switch]$PreserveDataDisk,
    [switch]$Start,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Administrator
{
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated Windows PowerShell session.'
    }
}

function Resolve-FullPath([string]$Path)
{
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Assert-PathWithin([string]$Path, [string]$Directory)
{
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullDirectory = [System.IO.Path]::GetFullPath($Directory).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullDirectory, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify '$fullPath' because it is outside '$fullDirectory'."
    }
}

function Invoke-Checked([scriptblock]$Operation, [string]$Description)
{
    & $Operation
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw 'This workflow must run in Windows PowerShell on the Hyper-V host.'
}
Assert-Administrator

$requiredCmdlets = @(
    'Get-VM', 'Stop-VM', 'Remove-VM', 'New-VM', 'Set-VM', 'Set-VMMemory',
    'Set-VMProcessor', 'Set-VMFirmware', 'Add-VMHardDiskDrive', 'Set-VMComPort',
    'Get-VMHardDiskDrive', 'Get-VMSnapshot', 'Get-VHD', 'New-VHD'
)
foreach ($cmdlet in $requiredCmdlets) {
    if (-not (Get-Command $cmdlet -ErrorAction SilentlyContinue)) {
        throw "Required Hyper-V cmdlet '$cmdlet' is unavailable."
    }
}
if (-not (Get-Command wsl.exe -CommandType Application -ErrorAction SilentlyContinue)) {
    throw 'wsl.exe is required to build the current InferenceOS source tree.'
}

$repository = [System.IO.Path]::GetFullPath((Resolve-FullPath $RepositoryRoot)).TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $repository 'CMakePresets.json') -PathType Leaf)) {
    throw "'$repository' is not an InferenceOS source tree."
}
$artifactDirectory = Join-Path $repository "build/$BuildPreset/artifacts"
$bootVhdx = Join-Path $artifactDirectory 'inferenceos-hyperv-boot.vhdx'
$dataVhdx = Join-Path $artifactDirectory 'inferenceos-hyperv-data.vhdx'
$loader = Join-Path $artifactDirectory 'BOOTX64.EFI'
$kernel = Join-Path $artifactDirectory 'kernel.elf'
$modules = Join-Path $artifactDirectory 'system-modules'
$bootBuilder = Join-Path $repository 'tools/image/build_hyperv_boot_vhdx.ps1'
$dataBuilder = Join-Path $repository 'tools/image/create_hyperv_data_vhdx.ps1'
$profileTest = Join-Path $repository 'tests/system/hyperv_profile_test.ps1'

Assert-PathWithin $bootVhdx $artifactDirectory
Assert-PathWithin $dataVhdx $artifactDirectory
foreach ($requiredFile in $bootBuilder,$dataBuilder,$profileTest) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required repository script '$requiredFile' is missing."
    }
}
if (($DataDiskSizeBytes % 512) -ne 0) {
    throw 'DataDiskSizeBytes must be aligned to 512 bytes.'
}
$description = if ($PreserveDataDisk) {
    "replace VM '$VmName' and its boot VHDX while preserving its data VHDX"
} else {
    "delete VM '$VmName' and its generated Hyper-V disks, then recreate them"
}
if (-not $Force) {
    throw "This operation is destructive. Re-run with -Force to $description."
}

# Build first so a compilation failure cannot destroy the existing VM.
$wslRepository = (& wsl.exe -e wslpath -u -a $repository 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($wslRepository)) {
    throw "WSL could not translate repository path '$repository': $wslRepository"
}
if ($wslRepository.Contains("'")) {
    throw 'Repository paths containing a single quote are not supported by this build wrapper.'
}
if (-not [string]::IsNullOrWhiteSpace($WslToolEnvironmentFile)) {
    if (-not $WslToolEnvironmentFile.StartsWith('/') -or
        $WslToolEnvironmentFile.Contains("'")) {
        throw 'WslToolEnvironmentFile must be an absolute WSL path without a single quote.'
    }
    $environmentCommand = ". '$WslToolEnvironmentFile' && "
} else {
    $environmentCommand =
        '. "${INFERENCEOS_ENVIRONMENT_FILE:-${HOME}/.local/share/inferenceos/tools/environment.sh}" && '
}
$buildCommand = $environmentCommand + "cd '$wslRepository' && cmake --preset $BuildPreset && " +
    "cmake --build --preset $BuildPreset --target inferenceos-image"
Write-Host "Building current InferenceOS source with preset '$BuildPreset'..."
Invoke-Checked { & wsl.exe -e bash -lc $buildCommand } 'InferenceOS build'

foreach ($artifact in $loader,$kernel) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "The build did not produce '$artifact'."
    }
}
if (-not (Test-Path -LiteralPath $modules -PathType Container)) {
    throw "The build did not produce module directory '$modules'."
}
if ($PreserveDataDisk) {
    if (-not (Test-Path -LiteralPath $dataVhdx -PathType Leaf)) {
        throw "Cannot preserve missing data VHDX '$dataVhdx'."
    }
    $dataDisk = Get-VHD -Path $dataVhdx
    if ($dataDisk.Size -lt 50000000000 -or $dataDisk.LogicalSectorSize -ne 512) {
        throw "Data VHDX '$dataVhdx' does not satisfy the InferenceOS capacity/sector contract."
    }
}

if (-not $PSCmdlet.ShouldProcess($VmName, $description)) { return }

[System.IO.Directory]::CreateDirectory($artifactDirectory) | Out-Null
$staleDiskPaths = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
[void]$staleDiskPaths.Add([System.IO.Path]::GetFullPath($bootVhdx))
if (-not $PreserveDataDisk) {
    [void]$staleDiskPaths.Add([System.IO.Path]::GetFullPath($dataVhdx))
}

$oldVm = Get-VM -Name $VmName -ErrorAction SilentlyContinue
if ($null -ne $oldVm) {
    $snapshots = @(Get-VMSnapshot -VMName $VmName -ErrorAction SilentlyContinue)
    if ($PreserveDataDisk -and $snapshots.Count -ne 0) {
        throw "Cannot safely preserve '$dataVhdx' while VM '$VmName' has checkpoints. Remove or merge the checkpoints first."
    }
    if ($PreserveDataDisk) {
        $attachedDataDisk = @(Get-VMHardDiskDrive -VMName $VmName | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_.Path) -and
            [System.IO.Path]::GetFullPath($_.Path) -eq [System.IO.Path]::GetFullPath($dataVhdx)
        })
        if ($attachedDataDisk.Count -ne 1) {
            throw "VM '$VmName' must have exactly one attachment to '$dataVhdx' before it can be preserved."
        }
    }
    foreach ($drive in @(Get-VMHardDiskDrive -VMName $VmName -ErrorAction SilentlyContinue)) {
        if ([string]::IsNullOrWhiteSpace($drive.Path)) { continue }
        $candidate = [System.IO.Path]::GetFullPath($drive.Path)
        $leaf = [System.IO.Path]::GetFileName($candidate)
        if ((-not $PreserveDataDisk -or $candidate -ine [System.IO.Path]::GetFullPath($dataVhdx)) -and
            [System.IO.Path]::GetDirectoryName($candidate) -ieq $artifactDirectory -and
            $leaf -match '^inferenceos-hyperv-(boot|data)(?:[-_.].*)?[.]a?vhdx$') {
            [void]$staleDiskPaths.Add($candidate)
        }
    }
    foreach ($snapshot in $snapshots) {
        foreach ($drive in @(Get-VMHardDiskDrive -VMSnapshot $snapshot -ErrorAction SilentlyContinue)) {
            if ([string]::IsNullOrWhiteSpace($drive.Path)) { continue }
            $candidate = [System.IO.Path]::GetFullPath($drive.Path)
            $leaf = [System.IO.Path]::GetFileName($candidate)
            if ((-not $PreserveDataDisk -or $candidate -ine [System.IO.Path]::GetFullPath($dataVhdx)) -and
                [System.IO.Path]::GetDirectoryName($candidate) -ieq $artifactDirectory -and
                $leaf -match '^inferenceos-hyperv-(boot|data)(?:[-_.].*)?[.]a?vhdx$') {
                [void]$staleDiskPaths.Add($candidate)
            }
        }
    }
    if ($oldVm.State -ne 'Off') {
        Stop-VM -Name $VmName -TurnOff -Confirm:$false
        do {
            Start-Sleep -Milliseconds 100
            $oldVm = Get-VM -Name $VmName
        } while ($oldVm.State -ne 'Off')
    }
    Remove-VM -Name $VmName -Force
}

foreach ($diskPath in $staleDiskPaths) {
    Assert-PathWithin $diskPath $artifactDirectory
    if (Test-Path -LiteralPath $diskPath -PathType Leaf) {
        Remove-Item -LiteralPath $diskPath -Force
    }
}
foreach ($manifest in @("$bootVhdx.manifest.json") + @(
    if (-not $PreserveDataDisk) { "$dataVhdx.manifest.json" }
)) {
    Assert-PathWithin $manifest $artifactDirectory
    Remove-Item -LiteralPath $manifest -Force -ErrorAction SilentlyContinue
}

Write-Host $(if ($PreserveDataDisk) {
    'Creating a fresh boot VHDX and preserving the existing data VHDX...'
} else {
    'Creating fresh boot and data VHDXs...'
})
& $bootBuilder -LoaderPath $loader -KernelPath $kernel `
    -ModuleDirectory $modules -OutputPath $bootVhdx -Force | Out-Null
if (-not $PreserveDataDisk) {
    & $dataBuilder -OutputPath $dataVhdx -SizeBytes $DataDiskSizeBytes -Force | Out-Null
}

Write-Host "Creating Generation 2 VM '$VmName'..."
New-VM -Name $VmName -Generation 2 -MemoryStartupBytes $MemoryStartupBytes -NoVHD | Out-Null
Set-VMMemory -VMName $VmName -DynamicMemoryEnabled $false
Set-VMProcessor -VMName $VmName -Count 1
Set-VM -Name $VmName -AutomaticCheckpointsEnabled $false
Set-VMFirmware -VMName $VmName -EnableSecureBoot Off -PauseAfterBootFailure On
Add-VMHardDiskDrive -VMName $VmName -ControllerType SCSI `
    -ControllerNumber 0 -ControllerLocation 0 -Path $bootVhdx
Add-VMHardDiskDrive -VMName $VmName -ControllerType SCSI `
    -ControllerNumber 0 -ControllerLocation 1 -Path $dataVhdx
$bootDrive = Get-VMHardDiskDrive -VMName $VmName | Where-Object {
    [System.IO.Path]::GetFullPath($_.Path) -eq [System.IO.Path]::GetFullPath($bootVhdx)
}
if ($null -eq $bootDrive) { throw 'The recreated VM is missing its boot VHDX.' }
Set-VMFirmware -VMName $VmName -FirstBootDevice $bootDrive
Set-VMComPort -VMName $VmName -Number 1 -Path "\\.\pipe\$VmName-COM1"
Set-VMComPort -VMName $VmName -Number 2 -Path "\\.\pipe\$VmName-COM2"

$profile = & $profileTest -VmName $VmName -BootVhdx $bootVhdx -DataVhdx $dataVhdx |
    ConvertFrom-Json
if ($profile.status -ne 'profile-valid') { throw 'The recreated VM failed profile validation.' }

if ($Start) { Start-VM -Name $VmName }

[pscustomobject][ordered]@{
    Status = 'created'
    VmName = $VmName
    VmState = (Get-VM -Name $VmName).State.ToString()
    Generation = 2
    BuildPreset = $BuildPreset
    BootVhdx = $bootVhdx
    DataVhdx = $dataVhdx
    DataDiskPreserved = [bool]$PreserveDataDisk
    Com1 = "\\.\pipe\$VmName-COM1"
    Com2 = "\\.\pipe\$VmName-COM2"
}
