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

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runner = Join-Path $repositoryRoot 'tools/test/run_qemu_tests.ps1'
$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $repositoryRoot 'build/system/display-safe-file-commands'
} else {
    [System.IO.Path]::GetFullPath($ArtifactDirectory)
}
$plan = [ordered]@{
    SchemaVersion = 1
    GuestAction = 'cui_command_audit'
    RequiredMarker = 'INFERENCEOS:CUI_COMMAND_AUDIT_PASS'
    AffectedCommands = @('cd', 'pwd', 'create', 'write', 'append', 'cat', 'rename', 'delete', 'dir', 'fileinfo', 'hashinfo', 'fatinfo')
    OperandContract = 'extension-hidden display path'
}

if ($DryRun) {
    [pscustomobject]$plan
    return
}
if (-not [System.IO.File]::Exists($runner)) {
    throw "QEMU story-test runner '$runner' is missing."
}

$parameters = @{
    EspPath = [System.IO.Path]::GetFullPath($EspPath)
    PersistentDiskPath = [System.IO.Path]::GetFullPath($PersistentDiskPath)
    ArtifactDirectory = $artifactRoot
    RunName = 'display-safe-file-commands'
    TimeoutSeconds = $TimeoutSeconds
    RequiredMarker = @($plan.RequiredMarker)
    ForbiddenMarker = @('INFERENCEOS:CUI_COMMAND_AUDIT_FAIL')
    TestAction = $plan.GuestAction
    RetainSuccessfulArtifacts = $true
}
if (-not [string]::IsNullOrWhiteSpace($OvmfCodePath)) {
    $parameters.OvmfCodePath = $OvmfCodePath
}
if (-not [string]::IsNullOrWhiteSpace($OvmfVarsPath)) {
    $parameters.OvmfVarsPath = $OvmfVarsPath
}
if (-not [string]::IsNullOrWhiteSpace($QemuPath)) {
    $parameters.QemuPath = $QemuPath
}
if ($SkipVersionCheck) { $parameters.SkipVersionCheck = $true }

& $runner @parameters
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "Display-safe file-command suite passed. Artifacts: '$artifactRoot'."
