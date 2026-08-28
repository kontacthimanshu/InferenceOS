[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$VmName,
    [Parameter(Mandatory)][string]$BootVhdx,
    [Parameter(Mandatory)][string]$DataVhdx
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$vm = Get-VM -Name $VmName -ErrorAction Stop
if ($vm.Generation -ne 2) { throw "VM '$VmName' must be Generation 2." }
if ($vm.DynamicMemoryEnabled) { throw "VM '$VmName' must use static memory." }
if ($vm.AutomaticCheckpointsEnabled) { throw "VM '$VmName' must disable automatic checkpoints." }
$firmware = Get-VMFirmware -VMName $VmName
if ($firmware.SecureBoot -ne 'Off') { throw "VM '$VmName' must disable Secure Boot." }

$expected = @(
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BootVhdx),
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DataVhdx)
)
$attached = @(Get-VMHardDiskDrive -VMName $VmName | ForEach-Object {
    [System.IO.Path]::GetFullPath($_.Path)
})
foreach ($path in $expected) {
    if ($attached -notcontains $path) { throw "VM '$VmName' is missing '$path'." }
}

[pscustomobject]@{
    vm_name = $VmName
    generation = $vm.Generation
    static_memory = -not $vm.DynamicMemoryEnabled
    secure_boot = $firmware.SecureBoot.ToString()
    automatic_checkpoints = $vm.AutomaticCheckpointsEnabled
    boot_vhdx = $expected[0]
    data_vhdx = $expected[1]
    status = 'profile-valid'
} | ConvertTo-Json -Depth 3
