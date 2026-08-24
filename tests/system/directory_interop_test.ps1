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

$scriptRoot = Split-Path -Parent $PSCommandPath
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '../..'))
$runner = Join-Path $repositoryRoot 'tools/test/run_qemu_tests.ps1'
$minimumBytes = [uint64]50000000000
$terminalMarker = 'INFERENCEOS:DIRECTORY_INTEROP_PASS'
$failureMarker = 'INFERENCEOS:DIRECTORY_INTEROP_FAIL'
$orderedMarkers = @(
    'INFERENCEOS:CUI_READY',
    'INFERENCEOS:DIRECTORY_CREATE interface=cui path=/DOCS result=ok',
    'INFERENCEOS:DIRECTORY_CHANGE interface=cui input=/DOCS cwd=/DOCS',
    'INFERENCEOS:DIRECTORY_CHANGE interface=cui input=. cwd=/DOCS',
    'INFERENCEOS:FILE_CREATE interface=cui path=/DOCS/REPORT.TXT result=ok',
    'INFERENCEOS:DIRECTORY_REFRESH interface=gui path=/DOCS entries=1',
    'INFERENCEOS:DIRECTORY_ENTRY interface=gui path=/DOCS name=REPORT kind=file',
    'INFERENCEOS:DIRECTORY_REMOVE interface=cui path=/DOCS result=not_empty',
    'INFERENCEOS:FILE_DELETE interface=gui path=/DOCS/REPORT.TXT result=ok',
    'INFERENCEOS:DIRECTORY_REFRESH interface=gui path=/DOCS entries=0',
    'INFERENCEOS:DIRECTORY_CHANGE interface=cui input=.. cwd=/',
    'INFERENCEOS:DIRECTORY_CHANGE interface=cui input=.. cwd=/',
    'INFERENCEOS:DIRECTORY_NAVIGATE interface=gui input=.. path=/',
    'INFERENCEOS:DIRECTORY_REMOVE interface=cui path=/DOCS result=ok',
    'INFERENCEOS:DIRECTORY_REFRESH interface=gui path=/ entries=0 docs_present=0',
    $terminalMarker
)

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Assert-InOrder([string]$Transcript, [string[]]$Markers) {
    $offset = 0
    foreach ($marker in $Markers) {
        $position = $Transcript.IndexOf($marker, $offset, [StringComparison]::Ordinal)
        if ($position -lt 0) {
            throw "Ordered directory marker '$marker' was not observed after offset $offset."
        }
        $offset = $position + $marker.Length
    }
}

if (-not [System.IO.File]::Exists($runner)) {
    throw "QEMU story-test runner '$runner' is missing."
}
$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $repositoryRoot 'build/system/directory-interop'
} else {
    [System.IO.Path]::GetFullPath($ArtifactDirectory)
}
$suiteId = "directory-interop-$([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))-$PID"
$suiteDirectory = Join-Path $artifactRoot $suiteId
$runRoot = Join-Path $suiteDirectory 'runs'
[System.IO.Directory]::CreateDirectory($runRoot) | Out-Null
$planPath = Join-Path $suiteDirectory 'suite-plan.json'
$plan = [ordered]@{
    SchemaVersion = 1
    SuiteId = $suiteId
    GuestAction = 'directory_interop'
    Interfaces = @('standalone_cui', 'shell_file_explorer')
    SharedNamespace = '/'
    Scenario = [ordered]@{
        Directory = '/DOCS'
        InternalFile = '/DOCS/REPORT.TXT'
        GuiDisplayName = 'REPORT'
        RequireNonEmptyRemovalFailure = $true
        RequireRootClamp = $true
        RequireFinalCrossInterfaceAbsence = $true
    }
    OrderedMarkers = $orderedMarkers
}
Write-Utf8NoBom $planPath (($plan | ConvertTo-Json -Depth 8) + "`n")

if ($DryRun) {
    [pscustomobject]@{
        SuiteDirectory = $suiteDirectory
        PlanPath = $planPath
        GuestAction = $plan.GuestAction
        MarkerCount = $orderedMarkers.Count
    }
    return
}

$disk = [System.IO.Path]::GetFullPath($PersistentDiskPath)
if (-not [System.IO.File]::Exists($disk)) { throw "Persistent disk '$disk' does not exist." }
$diskBytes = [uint64](Get-Item -LiteralPath $disk).Length
if ($diskBytes -lt $minimumBytes) {
    throw "Persistent disk is $diskBytes bytes; at least $minimumBytes are required."
}
if (($diskBytes % 512) -ne 0) { throw 'Persistent disk size must be a multiple of 512 bytes.' }

$runnerParameters = @{
    EspPath = [System.IO.Path]::GetFullPath($EspPath)
    PersistentDiskPath = $disk
    ArtifactDirectory = $runRoot
    RunName = 'directory-interop'
    TimeoutSeconds = $TimeoutSeconds
    RequiredMarker = @($terminalMarker)
    ForbiddenMarker = @($failureMarker)
    RetainSuccessfulArtifacts = $true
    ExtraQemuArgumentJson = ConvertTo-Json -Compress -InputObject ([string[]]@(
        '-fw_cfg', 'name=opt/inferenceos/test_action,string=directory_interop'
    ))
}
if (-not [string]::IsNullOrWhiteSpace($OvmfCodePath)) {
    $runnerParameters.OvmfCodePath = $OvmfCodePath
}
if (-not [string]::IsNullOrWhiteSpace($OvmfVarsPath)) {
    $runnerParameters.OvmfVarsPath = $OvmfVarsPath
}
if (-not [string]::IsNullOrWhiteSpace($QemuPath)) { $runnerParameters.QemuPath = $QemuPath }
if ($SkipVersionCheck) { $runnerParameters.SkipVersionCheck = $true }

$passed = $false
$failure = $null
$runArtifact = $null
try {
    & $runner @runnerParameters
    if ($LASTEXITCODE -ne 0) {
        throw "QEMU story-test runner exited with code $LASTEXITCODE."
    }
    $runArtifact = Get-ChildItem -LiteralPath $runRoot -Directory -Filter 'directory-interop-*' |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if ($null -eq $runArtifact) { throw 'The runner retained no directory-interop artifacts.' }
    $serialPath = Join-Path $runArtifact.FullName 'serial.log'
    $resultPath = Join-Path $runArtifact.FullName 'result.json'
    if (-not [System.IO.File]::Exists($serialPath) -or
        -not [System.IO.File]::Exists($resultPath)) {
        throw 'The runner artifacts are incomplete.'
    }
    $runnerResult = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
    if (-not $runnerResult.Passed) { throw 'The runner manifest reports failure.' }
    $transcript = Get-Content -Raw -LiteralPath $serialPath
    Assert-InOrder -Transcript $transcript -Markers $orderedMarkers
    $guiEntry = [regex]::Match(
        $transcript,
        '(?m)^INFERENCEOS:DIRECTORY_ENTRY interface=gui path=/DOCS ' +
        'name=(?<name>[^ ]+) kind=file\s*$'
    )
    if (-not $guiEntry.Success -or $guiEntry.Groups['name'].Value -ne 'REPORT') {
        throw 'File Explorer did not expose exactly the extension-hidden REPORT entry.'
    }
    $passed = $true
} catch {
    $failure = $_.Exception.Message
} finally {
    $suiteResult = [ordered]@{
        SchemaVersion = 1
        SuiteId = $suiteId
        Passed = $passed
        Failure = $failure
        DiskPath = $disk
        DiskBytes = $diskBytes
        RunArtifactDirectory = if ($null -eq $runArtifact) { $null } else { $runArtifact.FullName }
        PlanPath = $planPath
    }
    Write-Utf8NoBom (Join-Path $suiteDirectory 'suite-result.json') `
        (($suiteResult | ConvertTo-Json -Depth 6) + "`n")
}

if (-not $passed) {
    Write-Error "Directory interoperability suite failed: $failure Artifacts: '$suiteDirectory'."
    exit 1
}
Write-Output "Directory interoperability suite passed. Artifacts: '$suiteDirectory'."
