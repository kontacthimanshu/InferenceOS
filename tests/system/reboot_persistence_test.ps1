[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EspPath,
    [Parameter(Mandatory)][string]$PersistentDiskPath,
    [string]$OvmfCodePath = $env:INFERENCEOS_OVMF_CODE,
    [string]$OvmfVarsPath = $env:INFERENCEOS_OVMF_VARS,
    [string]$QemuPath,
    [string]$ArtifactDirectory,
    [ValidateRange(20, 100)][uint32]$CycleCount = 20,
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
$failureMarker = 'INFERENCEOS:PERSISTENCE_CYCLE_FAIL'

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Assert-InOrder([string]$Transcript, [string[]]$Markers) {
    $offset = 0
    foreach ($marker in $Markers) {
        $position = $Transcript.IndexOf($marker, $offset, [StringComparison]::Ordinal)
        if ($position -lt 0) {
            throw "Ordered persistence marker '$marker' was not observed after offset $offset."
        }
        $offset = $position + $marker.Length
    }
}

function Get-CycleContract([uint32]$Cycle) {
    $ordinal = '{0:D2}' -f $Cycle
    $cuiInternal = "CUI$ordinal.TXT"
    $cuiDisplay = "CUI$ordinal"
    $guiInternal = "GUI$ordinal.TXT"
    $renamedInternal = "REN$ordinal.TXT"
    $renamedDisplay = "REN$ordinal"
    $cuiContent = "CUI-CYCLE-$ordinal"
    $guiContent = "GUI-CYCLE-$ordinal"
    $expectedEntries = [uint32]($Cycle * 2)
    [pscustomobject][ordered]@{
        Cycle = $Cycle
        Ordinal = $ordinal
        CuiInternalName = $cuiInternal
        CuiDisplayName = $cuiDisplay
        CuiContent = $cuiContent
        GuiInternalName = $guiInternal
        RenamedInternalName = $renamedInternal
        RenamedDisplayName = $renamedDisplay
        GuiContent = $guiContent
        ExpectedEntryCount = $expectedEntries
        TerminalMarker = "INFERENCEOS:PERSISTENCE_CYCLE_PASS cycle=$Cycle"
        OrderedMarkers = @(
            "INFERENCEOS:PERSISTENCE_CYCLE_BEGIN cycle=$Cycle",
            "INFERENCEOS:PERSISTENCE_REMOUNT cycle=$Cycle state=read_write",
            "INFERENCEOS:CUI_CREATE cycle=$Cycle name=$cuiInternal content=$cuiContent",
            "INFERENCEOS:GUI_TERMINAL_READ cycle=$Cycle name=$cuiInternal content=$cuiContent",
            "INFERENCEOS:FILE_EXPLORER_ENTRY cycle=$Cycle name=$cuiDisplay",
            "INFERENCEOS:GUI_TERMINAL_CREATE cycle=$Cycle name=$guiInternal content=$guiContent",
            "INFERENCEOS:GUI_TERMINAL_RENAME cycle=$Cycle from=$guiInternal to=$renamedInternal",
            "INFERENCEOS:CUI_READ cycle=$Cycle name=$renamedInternal content=$guiContent",
            "INFERENCEOS:FILE_EXPLORER_ENTRY cycle=$Cycle name=$renamedDisplay",
            "INFERENCEOS:PERSISTED_CHECK cycle=$Cycle entries=$expectedEntries interfaces=cui,gui_terminal,file_explorer",
            "INFERENCEOS:SYNC_OK cycle=$Cycle",
            "INFERENCEOS:PERSISTENCE_CYCLE_PASS cycle=$Cycle"
        )
    }
}

if (-not [System.IO.File]::Exists($runner)) {
    throw "QEMU story-test runner '$runner' is missing."
}
$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $repositoryRoot 'build/system/reboot-persistence'
} else {
    [System.IO.Path]::GetFullPath($ArtifactDirectory)
}
$suiteId = "reboot-persistence-$([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))-$PID"
$suiteDirectory = Join-Path $artifactRoot $suiteId
$runRoot = Join-Path $suiteDirectory 'runs'
[System.IO.Directory]::CreateDirectory($runRoot) | Out-Null

$contracts = @(
    for ($cycle = 1; $cycle -le $CycleCount; ++$cycle) {
        Get-CycleContract -Cycle $cycle
    }
)
$plan = [ordered]@{
    SchemaVersion = 1
    SuiteId = $suiteId
    CycleCount = $CycleCount
    RebootModel = 'one fresh QEMU boot per cycle against the same persistent disk'
    GuestAction = 'reboot_persistence_cycle'
    Interfaces = @('standalone_cui', 'gui_terminal', 'shell_file_explorer')
    NamespaceRules = [ordered]@{
        EntriesAddedPerCycle = 2
        ExtensionsHiddenInFileExplorer = $true
        DigestMustChainAcrossReboots = $true
    }
    Cycles = $contracts
}
$planPath = Join-Path $suiteDirectory 'suite-plan.json'
Write-Utf8NoBom $planPath (($plan | ConvertTo-Json -Depth 10) + "`n")

if ($DryRun) {
    [pscustomobject]@{
        SuiteDirectory = $suiteDirectory
        PlanPath = $planPath
        CycleCount = $CycleCount
        ExpectedFinalEntryCount = [uint32]($CycleCount * 2)
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

$results = [System.Collections.Generic.List[object]]::new()
$suitePassed = $true
$suiteFailure = $null
$previousDigest = 'none'
$startedUtc = [DateTime]::UtcNow
try {
    foreach ($contract in $contracts) {
        Write-Host "[$($contract.Cycle)/$CycleCount] persistent reboot cycle"
        $runName = 'reboot-persistence-{0:D2}' -f $contract.Cycle
        $runnerParameters = @{
            EspPath = [System.IO.Path]::GetFullPath($EspPath)
            PersistentDiskPath = $disk
            ArtifactDirectory = $runRoot
            RunName = $runName
            TimeoutSeconds = $TimeoutSeconds
            RequiredMarker = @($contract.TerminalMarker)
            ForbiddenMarker = @($failureMarker)
            RetainSuccessfulArtifacts = $true
            ExtraQemuArgumentJson = ConvertTo-Json -Compress -InputObject ([string[]]@(
                '-fw_cfg', 'name=opt/inferenceos/test_action,string=reboot_persistence_cycle',
                '-fw_cfg', "name=opt/inferenceos/persistence_cycle,string=$($contract.Cycle)"
            ))
        }
        if (-not [string]::IsNullOrWhiteSpace($OvmfCodePath)) {
            $runnerParameters.OvmfCodePath = $OvmfCodePath
        }
        if (-not [string]::IsNullOrWhiteSpace($OvmfVarsPath)) {
            $runnerParameters.OvmfVarsPath = $OvmfVarsPath
        }
        if (-not [string]::IsNullOrWhiteSpace($QemuPath)) {
            $runnerParameters.QemuPath = $QemuPath
        }
        if ($SkipVersionCheck) { $runnerParameters.SkipVersionCheck = $true }

        & $runner @runnerParameters
        if ($LASTEXITCODE -ne 0) {
            throw "QEMU story-test runner exited with code $LASTEXITCODE for cycle $($contract.Cycle)."
        }
        $runArtifact = Get-ChildItem -LiteralPath $runRoot -Directory -Filter "$runName-*" |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        if ($null -eq $runArtifact) {
            throw "The runner retained no artifact directory for cycle $($contract.Cycle)."
        }
        $serialPath = Join-Path $runArtifact.FullName 'serial.log'
        $resultPath = Join-Path $runArtifact.FullName 'result.json'
        if (-not [System.IO.File]::Exists($serialPath) -or
            -not [System.IO.File]::Exists($resultPath)) {
            throw "Runner artifacts are incomplete for cycle $($contract.Cycle)."
        }
        $runnerResult = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
        if (-not $runnerResult.Passed) {
            throw "Runner manifest reports cycle $($contract.Cycle) failed."
        }
        $transcript = Get-Content -Raw -LiteralPath $serialPath
        Assert-InOrder -Transcript $transcript -Markers $contract.OrderedMarkers
        $namespacePattern = '(?m)^INFERENCEOS:PERSISTENCE_NAMESPACE cycle=' +
            $contract.Cycle + ' entries=' + $contract.ExpectedEntryCount +
            ' digest=(?<digest>[0-9A-F]{16}) prior_digest=(?<prior>none|[0-9A-F]{16})\s*$'
        $namespace = [regex]::Match($transcript, $namespacePattern)
        if (-not $namespace.Success) {
            throw "A valid namespace digest marker was not found for cycle $($contract.Cycle)."
        }
        if ($namespace.Groups['prior'].Value -ne $previousDigest) {
            throw "Namespace digest continuity failed at cycle $($contract.Cycle)."
        }
        $previousDigest = $namespace.Groups['digest'].Value
        $results.Add([pscustomobject][ordered]@{
            Cycle = $contract.Cycle
            Passed = $true
            EntryCount = $contract.ExpectedEntryCount
            NamespaceDigest = $previousDigest
            ArtifactDirectory = $runArtifact.FullName
        })
    }
} catch {
    $suitePassed = $false
    $suiteFailure = $_.Exception.Message
} finally {
    $suiteResult = [ordered]@{
        SchemaVersion = 1
        SuiteId = $suiteId
        Passed = $suitePassed
        Failure = $suiteFailure
        StartedUtc = $startedUtc.ToString('o')
        FinishedUtc = [DateTime]::UtcNow.ToString('o')
        PlannedCycleCount = $CycleCount
        PassedCycleCount = $results.Count
        FinalNamespaceDigest = $previousDigest
        Results = [object[]]$results
        PlanPath = $planPath
    }
    Write-Utf8NoBom (Join-Path $suiteDirectory 'suite-result.json') `
        (($suiteResult | ConvertTo-Json -Depth 8) + "`n")
}

if (-not $suitePassed) {
    Write-Error "Reboot persistence suite failed: $suiteFailure Artifacts: '$suiteDirectory'."
    exit 1
}
Write-Output "Reboot persistence suite passed $CycleCount cycles. Artifacts: '$suiteDirectory'."
