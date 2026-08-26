[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EspPath,
    [Parameter(Mandatory)][string]$PersistentDiskPath,
    [string]$OvmfCodePath = $env:INFERENCEOS_OVMF_CODE,
    [string]$OvmfVarsPath = $env:INFERENCEOS_OVMF_VARS,
    [string]$QemuPath,
    [string]$ArtifactDirectory,
    [ValidateRange(1, 3600)][uint32]$TimeoutSeconds = 60,
    [switch]$SkipVersionCheck,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $PSCommandPath
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '../..'))
$runner = Join-Path $repositoryRoot 'tools/test/run_qemu_tests.ps1'
$terminalMarker = 'INFERENCEOS:FILE_EXPLORER_TEST_PASS'
$failureMarker = 'INFERENCEOS:FILE_EXPLORER_TEST_FAIL'

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Assert-ExactLine([string[]]$Lines, [string]$Expected) {
    if ($Expected -notin $Lines) { throw "Required File Explorer marker was absent: $Expected" }
}

if (-not [System.IO.File]::Exists($runner)) { throw "QEMU story-test runner '$runner' is missing." }
$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $repositoryRoot 'build/system/file-explorer'
} else {
    [System.IO.Path]::GetFullPath($ArtifactDirectory)
}
$suiteId = "file-explorer-$([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))-$PID"
$suiteDirectory = Join-Path $artifactRoot $suiteId
$runRoot = Join-Path $suiteDirectory 'runs'
[System.IO.Directory]::CreateDirectory($runRoot) | Out-Null
$planPath = Join-Path $suiteDirectory 'suite-plan.json'
$plan = [ordered]@{
    SchemaVersion = 1
    SuiteId = $suiteId
    GuestAction = 'file_explorer_fake_provider'
    Provider = 'fake'
    ExpectedEntries = @(
        @{ Handle = 7; Name = 'REPORT'; Icon = 'text' }
        @{ Handle = 9; Name = 'REPORT (2)'; Icon = 'generic-file' }
        @{ Handle = 11; Name = 'DOCS'; Icon = 'folder' }
    )
    InputSequence = @('pointer-select:9', 'keyboard-activate:9')
    ForbiddenOrdinaryMetadata = @(
        '.TXT', 'E771F04F', 'extension=', 'hash=', 'companion', 'cluster=', 'fat=', 'registry='
    )
}
Write-Utf8NoBom $planPath (($plan | ConvertTo-Json -Depth 6) + "`n")
if ($DryRun) {
    [pscustomobject]@{ SuiteDirectory = $suiteDirectory; PlanPath = $planPath; GuestAction = $plan.GuestAction }
    return
}

$runnerParameters = @{
    EspPath = [System.IO.Path]::GetFullPath($EspPath)
    PersistentDiskPath = [System.IO.Path]::GetFullPath($PersistentDiskPath)
    ArtifactDirectory = $runRoot
    RunName = 'file-explorer-fake-provider'
    TimeoutSeconds = $TimeoutSeconds
    RequiredMarker = @($terminalMarker)
    ForbiddenMarker = @($failureMarker)
    RetainSuccessfulArtifacts = $true
    TestAction = 'file_explorer_fake_provider'
}
if (-not [string]::IsNullOrWhiteSpace($OvmfCodePath)) { $runnerParameters.OvmfCodePath = $OvmfCodePath }
if (-not [string]::IsNullOrWhiteSpace($OvmfVarsPath)) { $runnerParameters.OvmfVarsPath = $OvmfVarsPath }
if (-not [string]::IsNullOrWhiteSpace($QemuPath)) { $runnerParameters.QemuPath = $QemuPath }
if ($SkipVersionCheck) { $runnerParameters.SkipVersionCheck = $true }

$passed = $false
$failure = $null
$runArtifact = $null
try {
    & $runner @runnerParameters
    if ($LASTEXITCODE -ne 0) { throw "QEMU story-test runner exited with code $LASTEXITCODE." }
    $runArtifact = Get-ChildItem -LiteralPath $runRoot -Directory -Filter 'file-explorer-fake-provider-*' |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if ($null -eq $runArtifact) { throw 'The runner retained no File Explorer artifact directory.' }
    $serialPath = Join-Path $runArtifact.FullName 'serial.log'
    if (-not [System.IO.File]::Exists($serialPath)) { throw 'The retained serial transcript is missing.' }
    $transcript = Get-Content -Raw -LiteralPath $serialPath
    $viewLines = @($transcript -split "`r?`n" | Where-Object {
        $_.StartsWith('INFERENCEOS:FILE_EXPLORER', [StringComparison]::Ordinal)
    })
    Assert-ExactLine $viewLines 'INFERENCEOS:FILE_EXPLORER_PROVIDER kind=fake'
    Assert-ExactLine $viewLines 'INFERENCEOS:FILE_EXPLORER_ENTRY handle=7 name=REPORT icon=text'
    Assert-ExactLine $viewLines 'INFERENCEOS:FILE_EXPLORER_ENTRY handle=9 name=REPORT (2) icon=generic-file'
    Assert-ExactLine $viewLines 'INFERENCEOS:FILE_EXPLORER_ENTRY handle=11 name=DOCS icon=folder'
    Assert-ExactLine $viewLines 'INFERENCEOS:FILE_EXPLORER_SELECTED input=pointer handle=9'
    Assert-ExactLine $viewLines 'INFERENCEOS:FILE_EXPLORER_ACTIVATED input=keyboard handle=9'
    Assert-ExactLine $viewLines 'INFERENCEOS:FILE_EXPLORER_PROPERTIES handle=9 name=REPORT (2) size=15'
    foreach ($forbidden in $plan.ForbiddenOrdinaryMetadata) {
        if ($viewLines -match [regex]::Escape($forbidden)) {
            throw "File Explorer output leaked forbidden ordinary metadata '$forbidden'."
        }
    }
    $passed = $true
} catch {
    $failure = $_.Exception.Message
} finally {
    $result = [ordered]@{
        SchemaVersion = 1
        SuiteId = $suiteId
        Passed = $passed
        Failure = $failure
        Provider = 'fake'
        RunArtifactDirectory = if ($null -eq $runArtifact) { $null } else { $runArtifact.FullName }
        PlanPath = $planPath
    }
    Write-Utf8NoBom (Join-Path $suiteDirectory 'suite-result.json') (($result | ConvertTo-Json -Depth 5) + "`n")
}
if (-not $passed) {
    Write-Error "File Explorer fake-provider suite failed: $failure Artifacts: '$suiteDirectory'."
    exit 1
}
Write-Output "File Explorer fake-provider suite passed. Artifacts: '$suiteDirectory'."
