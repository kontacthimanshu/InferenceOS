[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$VmName,
    [Parameter(Mandatory)][string]$BootVhdx,
    [Parameter(Mandatory)][string]$DataVhdx,
    [switch]$Start
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$profileTest = Join-Path (Split-Path -Parent $PSScriptRoot) '../tests/system/hyperv_profile_test.ps1'
& ([System.IO.Path]::GetFullPath($profileTest)) -VmName $VmName `
    -BootVhdx $BootVhdx -DataVhdx $DataVhdx
if ($Start) {
    if ((Get-VM -Name $VmName).State -eq 'Off') { Start-VM -Name $VmName }
    Write-Output "VM '$VmName' started. Complete the interactive persistence and disk-safety matrix in docs/hyperv.md."
}
