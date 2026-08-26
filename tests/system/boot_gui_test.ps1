[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EspPath,
    [Parameter(Mandatory)][string]$PersistentDiskPath,
    [string]$OvmfCodePath = $env:INFERENCEOS_OVMF_CODE,
    [string]$OvmfVarsPath = $env:INFERENCEOS_OVMF_VARS,
    [string]$QemuPath,
    [string]$ArtifactDirectory,
    [ValidateRange(1, 100)][uint32]$RunCount = 20,
    [ValidateRange(1, 3600)][uint32]$TimeoutSeconds = 60,
    [switch]$SkipVersionCheck,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $PSCommandPath
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '../..'))
$runner = Join-Path $repositoryRoot 'tools/test/run_qemu_tests.ps1'
$markers = [ordered]@{
    EarlySerialReady = 'INFERENCEOS:EARLY_SERIAL_READY'
    CuiReady = 'INFERENCEOS:CUI_READY'
    GuiReady = 'INFERENCEOS:GUI_READY'
    GuiUnavailable = 'INFERENCEOS:GUI_UNAVAILABLE'
    CuiRecoveryReady = 'INFERENCEOS:CUI_RECOVERY_READY'
}

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Convert-ToNativeArgumentString([string[]]$Arguments) {
    $quoted = foreach ($argument in $Arguments) {
        if ($argument.Length -gt 0 -and $argument -notmatch '[\s"]') { $argument; continue }
        $builder = [System.Text.StringBuilder]::new()
        [void]$builder.Append('"')
        $backslashes = 0
        foreach ($character in $argument.ToCharArray()) {
            if ($character -eq '\') {
                ++$backslashes
            } elseif ($character -eq '"') {
                [void]$builder.Append(('\' * ($backslashes * 2 + 1)))
                [void]$builder.Append('"')
                $backslashes = 0
            } else {
                if ($backslashes) { [void]$builder.Append(('\' * $backslashes)); $backslashes = 0 }
                [void]$builder.Append($character)
            }
        }
        if ($backslashes) { [void]$builder.Append(('\' * ($backslashes * 2))) }
        [void]$builder.Append('"')
        $builder.ToString()
    }
    return $quoted -join ' '
}

function New-TestCase(
    [string]$Name,
    [string]$Phase,
    [string]$TerminalMarker,
    [string[]]$OrderedMarkers,
    [string[]]$ForbiddenMarkers,
    [string]$TestAction,
    [string]$TestArgument
) {
    [pscustomobject][ordered]@{
        Name = $Name
        Phase = $Phase
        TerminalMarker = $TerminalMarker
        OrderedMarkers = $OrderedMarkers
        ForbiddenMarkers = $ForbiddenMarkers
        TestAction = $TestAction
        TestArgument = $TestArgument
    }
}

function Assert-MarkersInOrder([string]$Transcript, [string[]]$OrderedMarkers) {
    $offset = 0
    foreach ($marker in $OrderedMarkers) {
        $position = $Transcript.IndexOf($marker, $offset, [StringComparison]::Ordinal)
        if ($position -lt 0) { throw "Required ordered marker '$marker' was not observed after offset $offset." }
        $offset = $position + $marker.Length
    }
}

function Invoke-RunnerProcess([string[]]$Arguments) {
    $hostExecutable = (Get-Process -Id $PID).Path
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $hostExecutable
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    if ($null -ne $startInfo.PSObject.Properties['ArgumentList']) {
        foreach ($argument in $Arguments) { [void]$startInfo.ArgumentList.Add($argument) }
    } else {
        $startInfo.Arguments = Convert-ToNativeArgumentString $Arguments
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'The QEMU test-runner process did not start.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            StandardOutput = $stdout.GetAwaiter().GetResult()
            StandardError = $stderr.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
}

if (-not [System.IO.File]::Exists($runner)) { throw "QEMU story-test runner '$runner' is missing." }

$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $repositoryRoot 'build/system/boot-gui'
} else {
    [System.IO.Path]::GetFullPath($ArtifactDirectory)
}
$suiteId = "boot-gui-$([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))-$PID"
$suiteDirectory = Join-Path $artifactRoot $suiteId
$runDirectory = Join-Path $suiteDirectory 'runs'
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null

$cases = [System.Collections.Generic.List[object]]::new()
for ($index = 1; $index -le $RunCount; ++$index) {
    $cases.Add((New-TestCase `
        -Name ('boot-{0:D2}' -f $index) `
        -Phase 'boot' `
        -TerminalMarker $markers.CuiReady `
        -OrderedMarkers @($markers.EarlySerialReady, $markers.CuiReady) `
        -ForbiddenMarkers @($markers.GuiUnavailable) `
        -TestAction $null `
        -TestArgument $null))
}
for ($index = 1; $index -le $RunCount; ++$index) {
    $cases.Add((New-TestCase `
        -Name ('gui-{0:D2}' -f $index) `
        -Phase 'gui' `
        -TerminalMarker $markers.GuiReady `
        -OrderedMarkers @($markers.EarlySerialReady, $markers.CuiReady, $markers.GuiReady) `
        -ForbiddenMarkers @($markers.GuiUnavailable) `
        -TestAction 'start_gui' `
        -TestArgument $null))
}
foreach ($fault in @('framebuffer', 'font', 'pointer')) {
    $cases.Add((New-TestCase `
        -Name "recovery-$fault" `
        -Phase 'recovery' `
        -TerminalMarker $markers.CuiRecoveryReady `
        -OrderedMarkers @(
            $markers.EarlySerialReady,
            $markers.CuiReady,
            $markers.GuiUnavailable,
            $markers.CuiRecoveryReady
        ) `
        -ForbiddenMarkers @($markers.GuiReady) `
        -TestAction 'gui_recovery' `
        -TestArgument $fault))
}

$plan = [ordered]@{
    SchemaVersion = 1
    SuiteId = $suiteId
    RunCountPerSuccessPhase = $RunCount
    ExpectedCaseCount = [int]($RunCount * 2 + 3)
    RecoveryFaults = @('framebuffer', 'font', 'pointer')
    Cases = [object[]]$cases
}
$planPath = Join-Path $suiteDirectory 'suite-plan.json'
Write-Utf8NoBom $planPath (($plan | ConvertTo-Json -Depth 8) + "`n")

if ($DryRun) {
    [pscustomobject]@{
        SuiteDirectory = $suiteDirectory
        PlanPath = $planPath
        CaseCount = $cases.Count
        BootRuns = $RunCount
        GuiRuns = $RunCount
        RecoveryRuns = 3
    }
    return
}

$results = [System.Collections.Generic.List[object]]::new()
$suitePassed = $true
$suiteFailure = $null
$startedUtc = [DateTime]::UtcNow
try {
    foreach ($case in $cases) {
        Write-Host "[$($results.Count + 1)/$($cases.Count)] $($case.Name)"
        $arguments = [System.Collections.Generic.List[string]]::new()
        $arguments.AddRange([string[]]@(
            '-NoLogo', '-NoProfile', '-NonInteractive', '-File', $runner,
            '-EspPath', $EspPath,
            '-PersistentDiskPath', $PersistentDiskPath,
            '-ArtifactDirectory', $runDirectory,
            '-RunName', $case.Name,
            '-TimeoutSeconds', [string]$TimeoutSeconds,
            '-RequiredMarker', $case.TerminalMarker,
            '-RetainSuccessfulArtifacts'
        ))
        if (-not [string]::IsNullOrWhiteSpace($QemuPath)) {
            $arguments.AddRange([string[]]@('-QemuPath', $QemuPath))
        }
        if (-not [string]::IsNullOrWhiteSpace($OvmfCodePath)) {
            $arguments.AddRange([string[]]@('-OvmfCodePath', $OvmfCodePath))
        }
        if (-not [string]::IsNullOrWhiteSpace($OvmfVarsPath)) {
            $arguments.AddRange([string[]]@('-OvmfVarsPath', $OvmfVarsPath))
        }
        if ($SkipVersionCheck) { $arguments.Add('-SkipVersionCheck') }
        foreach ($marker in $case.ForbiddenMarkers) {
            $arguments.AddRange([string[]]@('-ForbiddenMarker', $marker))
        }
        if (-not [string]::IsNullOrWhiteSpace($case.TestAction)) {
            $arguments.AddRange([string[]]@('-TestAction', $case.TestAction))
            if (-not [string]::IsNullOrWhiteSpace($case.TestArgument)) {
                $arguments.AddRange([string[]]@('-TestArgument', $case.TestArgument))
            }
        }

        $runnerResult = Invoke-RunnerProcess ([string[]]$arguments)
        $caseArtifact = Get-ChildItem -LiteralPath $runDirectory -Directory -Filter "$($case.Name)-*" |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        if ($null -eq $caseArtifact) { throw "Runner produced no artifact directory for '$($case.Name)'." }
        $resultPath = Join-Path $caseArtifact.FullName 'result.json'
        $serialPath = Join-Path $caseArtifact.FullName 'serial.log'
        if ($runnerResult.ExitCode -ne 0) {
            throw "Runner failed for '$($case.Name)' with exit code $($runnerResult.ExitCode): $($runnerResult.StandardError.Trim())"
        }
        if (-not [System.IO.File]::Exists($resultPath) -or -not [System.IO.File]::Exists($serialPath)) {
            throw "Runner artifacts for '$($case.Name)' are incomplete."
        }
        $runnerManifest = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
        if (-not $runnerManifest.Passed) { throw "Runner manifest reports '$($case.Name)' failed." }
        $transcript = Get-Content -Raw -LiteralPath $serialPath
        Assert-MarkersInOrder $transcript $case.OrderedMarkers
        foreach ($marker in $case.ForbiddenMarkers) {
            if ($transcript.Contains($marker)) { throw "Forbidden marker '$marker' appeared in '$($case.Name)'." }
        }
        $results.Add([pscustomobject][ordered]@{
            Name = $case.Name
            Phase = $case.Phase
            Passed = $true
            ArtifactDirectory = $caseArtifact.FullName
        })
    }
} catch {
    $suitePassed = $false
    $suiteFailure = $_.Exception.Message
} finally {
    $phaseCounts = [ordered]@{}
    foreach ($phase in @('boot', 'gui', 'recovery')) {
        $phaseCounts[$phase] = @($results | Where-Object Phase -eq $phase).Count
    }
    $suiteResult = [ordered]@{
        SchemaVersion = 1
        SuiteId = $suiteId
        Passed = $suitePassed
        Failure = $suiteFailure
        StartedUtc = $startedUtc.ToString('o')
        FinishedUtc = [DateTime]::UtcNow.ToString('o')
        PlannedCaseCount = $cases.Count
        PassedCaseCount = $results.Count
        PassedByPhase = $phaseCounts
        Results = [object[]]$results
        PlanPath = $planPath
    }
    $resultPath = Join-Path $suiteDirectory 'suite-result.json'
    Write-Utf8NoBom $resultPath (($suiteResult | ConvertTo-Json -Depth 8) + "`n")
}

if (-not $suitePassed) {
    Write-Error "Boot/GUI/recovery suite failed: $suiteFailure Artifacts retained at '$suiteDirectory'."
    exit 1
}
Write-Output "Boot/GUI/recovery suite passed all $($cases.Count) cases. Artifacts: '$suiteDirectory'."
