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
$reporter = Join-Path $root 'tools/test/run_registry_benchmark.ps1'
$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $root 'build/system/registry-benchmark-qemu'
} else { [System.IO.Path]::GetFullPath($ArtifactDirectory) }
$parameters = @{
    EspPath = [System.IO.Path]::GetFullPath($EspPath)
    PersistentDiskPath = [System.IO.Path]::GetFullPath($PersistentDiskPath)
    ArtifactDirectory = $artifactRoot
    RunName = 'registry-benchmark'
    TimeoutSeconds = $TimeoutSeconds
    RequiredMarker = @('INFERENCEOS:REGISTRY_BENCHMARK_PASS samples=5')
    ForbiddenMarker = @('INFERENCEOS:REGISTRY_BENCHMARK_FAIL')
    TestAction = 'registry_benchmark'
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
$run = Get-ChildItem -LiteralPath $artifactRoot -Directory -Filter 'registry-benchmark-*' |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if ($null -eq $run) { throw 'The registry benchmark runner retained no evidence.' }
$serial = Join-Path $run.FullName 'serial.log'
$reportDirectory = Join-Path $run.FullName 'report'
$compiler = if ([string]::IsNullOrWhiteSpace($env:INFERENCEOS_COMPILER_VERSION)) {
    'GNU 16.2.0'
} else { $env:INFERENCEOS_COMPILER_VERSION }
& $reporter -TracePath $serial -OutputDirectory $reportDirectory `
    -BuildVersion 'qemu-guest' -CompilerVersion $compiler -QemuVersion '11.1.0' `
    -ImagePath ([System.IO.Path]::GetFullPath($EspPath)) | Out-Null
$report = Get-Content -Raw -LiteralPath (
    Join-Path $reportDirectory 'registry-benchmark-report.json'
) | ConvertFrom-Json
if (-not $report.correctness.matched -or $report.sample_count -ne 5) {
    throw 'The guest registry benchmark report is incomplete.'
}
Write-Output "Guest registry benchmark passed. Report: '$reportDirectory'."
