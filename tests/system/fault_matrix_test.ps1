[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EspPath,
    [Parameter(Mandatory)][string]$PersistentDiskPath,
    [string]$OvmfCodePath = $env:INFERENCEOS_OVMF_CODE,
    [string]$OvmfVarsPath = $env:INFERENCEOS_OVMF_VARS,
    [string]$QemuPath,
    [string]$ArtifactDirectory,
    [ValidateRange(1, 3600)][uint32]$TimeoutSeconds = 120,
    [switch]$SkipVersionCheck,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runner = Join-Path $root 'tools/test/run_qemu_tests.ps1'
$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $root 'build/system/fault-matrix'
} else { [System.IO.Path]::GetFullPath($ArtifactDirectory) }
$parameters = @{
    EspPath = [System.IO.Path]::GetFullPath($EspPath)
    PersistentDiskPath = [System.IO.Path]::GetFullPath($PersistentDiskPath)
    ArtifactDirectory = $artifactRoot
    RunName = 'fault-matrix'
    TimeoutSeconds = $TimeoutSeconds
    RequiredMarker = @('INFERENCEOS:FAULT_MATRIX_PASS')
    ForbiddenMarker = @('INFERENCEOS:FAULT_MATRIX_FAIL')
    TestAction = 'fault_matrix'
    RetainSuccessfulArtifacts = $true
    DryRun = [bool]$DryRun
}
if (-not [string]::IsNullOrWhiteSpace($OvmfCodePath)) { $parameters.OvmfCodePath = $OvmfCodePath }
if (-not [string]::IsNullOrWhiteSpace($OvmfVarsPath)) { $parameters.OvmfVarsPath = $OvmfVarsPath }
if (-not [string]::IsNullOrWhiteSpace($QemuPath)) { $parameters.QemuPath = $QemuPath }
if ($SkipVersionCheck) { $parameters.SkipVersionCheck = $true }
& $runner @parameters
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($DryRun) { return }
$run = Get-ChildItem -LiteralPath $artifactRoot -Directory -Filter 'fault-matrix-*' |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if ($null -eq $run) { throw 'The fault-matrix runner retained no evidence.' }
$trace = Get-Content -Raw -LiteralPath (Join-Path $run.FullName 'serial.log')
foreach ($class in @(
    'primary_crc','companion_crc','companion_hash','companion_orphan',
    'superblock_primary','superblock_backup','fat_loop','geometry_bounds',
    'registry_full','registry_stale','registry_corrupt'
)) {
    if (-not $trace.Contains("INFERENCEOS:FAULT_INJECTION class=$class result=contained fallback=bounded")) {
        throw "Missing bounded fault-injection evidence for '$class'."
    }
}
Write-Output "Guest fault matrix passed. Artifacts: '$($run.FullName)'."
