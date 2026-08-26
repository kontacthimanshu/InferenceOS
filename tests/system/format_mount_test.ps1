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

$minimumBytes = [uint64]50000000000
$scriptRoot = Split-Path -Parent $PSCommandPath
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '../..'))
$runner = Join-Path $repositoryRoot 'tools/test/run_qemu_tests.ps1'
$terminalMarker = 'INFERENCEOS:FORMAT_MOUNT_COMPLETE'
$failureMarker = 'INFERENCEOS:FORMAT_MOUNT_FAILED'
$orderedPrefixes = @(
    'INFERENCEOS:CUI_READY',
    'INFERENCEOS:FORMAT_OK',
    'INFERENCEOS:MOUNT_OK',
    'INFERENCEOS:FSINFO phase=initial',
    'INFERENCEOS:UNMOUNT_OK',
    'INFERENCEOS:REMOUNT_OK',
    'INFERENCEOS:FSINFO phase=remount',
    $terminalMarker
)

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

function Invoke-Runner([string[]]$Arguments) {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Process -Id $PID).Path
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
        if (-not $process.Start()) { throw 'The QEMU test runner did not start.' }
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

function Assert-InOrder([string]$Transcript, [string[]]$Prefixes) {
    $offset = 0
    foreach ($prefix in $Prefixes) {
        $position = $Transcript.IndexOf($prefix, $offset, [StringComparison]::Ordinal)
        if ($position -lt 0) { throw "Ordered marker '$prefix' was not observed after offset $offset." }
        $offset = $position + $prefix.Length
    }
}

function Read-FsInfo([string]$Transcript, [string]$Phase) {
    $pattern = '(?m)^INFERENCEOS:FSINFO phase=' + [regex]::Escape($Phase) +
        ' format_version=(?<version>\d+) total_bytes=(?<total>\d+)' +
        ' usable_bytes=(?<usable>\d+) free_bytes=(?<free>\d+)' +
        ' sector_size=(?<sector>\d+) cluster_size=(?<cluster>\d+)' +
        ' fat_sectors=(?<fat>\d+)\s*$'
    $match = [regex]::Match($Transcript, $pattern)
    if (-not $match.Success) { throw "A complete fsinfo marker for phase '$Phase' was not found." }
    return [pscustomobject]@{
        Version = [uint32]$match.Groups['version'].Value
        Total = [uint64]$match.Groups['total'].Value
        Usable = [uint64]$match.Groups['usable'].Value
        Free = [uint64]$match.Groups['free'].Value
        Sector = [uint32]$match.Groups['sector'].Value
        Cluster = [uint32]$match.Groups['cluster'].Value
        FatSectors = [uint32]$match.Groups['fat'].Value
    }
}

if (-not [System.IO.File]::Exists($runner)) { throw "QEMU story-test runner '$runner' is missing." }
$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $repositoryRoot 'build/system/format-mount'
} else {
    [System.IO.Path]::GetFullPath($ArtifactDirectory)
}
$suiteId = "format-mount-$([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))-$PID"
$suiteDirectory = Join-Path $artifactRoot $suiteId
[System.IO.Directory]::CreateDirectory($suiteDirectory) | Out-Null
$planPath = Join-Path $suiteDirectory 'suite-plan.json'
$plan = [ordered]@{
    SchemaVersion = 1
    SuiteId = $suiteId
    MinimumVolumeBytes = $minimumBytes
    GuestAction = 'format_mount_remount'
    OrderedMarkers = $orderedPrefixes
    RequiredGeometry = [ordered]@{ FormatVersion = 1; SectorSize = 512; ClusterSize = 4096 }
}
Write-Utf8NoBom $planPath (($plan | ConvertTo-Json -Depth 6) + "`n")
if ($DryRun) {
    [pscustomobject]@{ SuiteDirectory = $suiteDirectory; PlanPath = $planPath; GuestAction = $plan.GuestAction }
    return
}

$disk = [System.IO.Path]::GetFullPath($PersistentDiskPath)
if (-not [System.IO.File]::Exists($disk)) { throw "Persistent disk '$disk' does not exist." }
$diskBytes = [uint64](Get-Item -LiteralPath $disk).Length
if ($diskBytes -lt $minimumBytes) { throw "Persistent disk is $diskBytes bytes; at least $minimumBytes are required." }
if (($diskBytes % 512) -ne 0) { throw 'Persistent disk size must be a multiple of 512 bytes.' }

$runRoot = Join-Path $suiteDirectory 'runs'
[System.IO.Directory]::CreateDirectory($runRoot) | Out-Null
$arguments = [System.Collections.Generic.List[string]]::new()
$arguments.AddRange([string[]]@(
    '-NoLogo', '-NoProfile', '-NonInteractive', '-File', $runner,
    '-EspPath', $EspPath,
    '-PersistentDiskPath', $disk,
    '-ArtifactDirectory', $runRoot,
    '-RunName', 'format-mount-remount',
    '-TimeoutSeconds', [string]$TimeoutSeconds,
    '-RequiredMarker', $terminalMarker,
    '-ForbiddenMarker', $failureMarker,
    '-RetainSuccessfulArtifacts',
    '-TestAction', 'format_mount_remount'
))
if (-not [string]::IsNullOrWhiteSpace($QemuPath)) { $arguments.AddRange([string[]]@('-QemuPath', $QemuPath)) }
if (-not [string]::IsNullOrWhiteSpace($OvmfCodePath)) { $arguments.AddRange([string[]]@('-OvmfCodePath', $OvmfCodePath)) }
if (-not [string]::IsNullOrWhiteSpace($OvmfVarsPath)) { $arguments.AddRange([string[]]@('-OvmfVarsPath', $OvmfVarsPath)) }
if ($SkipVersionCheck) { $arguments.Add('-SkipVersionCheck') }

$passed = $false
$failure = $null
$runArtifact = $null
try {
    $runnerResult = Invoke-Runner ([string[]]$arguments)
    if ($runnerResult.ExitCode -ne 0) {
        throw "Runner exited with $($runnerResult.ExitCode): $($runnerResult.StandardError.Trim())"
    }
    $runArtifact = Get-ChildItem -LiteralPath $runRoot -Directory -Filter 'format-mount-remount-*' |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if ($null -eq $runArtifact) { throw 'The runner produced no retained artifact directory.' }
    $serialPath = Join-Path $runArtifact.FullName 'serial.log'
    $resultPath = Join-Path $runArtifact.FullName 'result.json'
    if (-not (Test-Path -LiteralPath $serialPath) -or -not (Test-Path -LiteralPath $resultPath)) {
        throw 'The runner did not retain the serial transcript and result manifest.'
    }
    $runnerManifest = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
    if (-not $runnerManifest.Passed) { throw 'The runner manifest reports failure.' }
    $transcript = Get-Content -Raw -LiteralPath $serialPath
    Assert-InOrder $transcript $orderedPrefixes
    $initial = Read-FsInfo $transcript 'initial'
    $remount = Read-FsInfo $transcript 'remount'
    foreach ($info in @($initial, $remount)) {
        if ($info.Version -ne 1 -or $info.Sector -ne 512 -or $info.Cluster -ne 4096) {
            throw 'Reported format version or allocation geometry is not version 1.'
        }
        if ($info.Total -ne $diskBytes -or $info.Usable -gt $info.Total -or $info.Free -gt $info.Usable) {
            throw 'Reported capacity/free-space values are inconsistent with the attached volume.'
        }
        if ($info.FatSectors -eq 0) { throw 'Reported FAT length is zero.' }
    }
    if ($initial.Total -ne $remount.Total -or $initial.Usable -ne $remount.Usable -or
        $initial.Free -ne $remount.Free -or $initial.FatSectors -ne $remount.FatSectors) {
        throw 'Unmount/remount changed the reported filesystem geometry or free-space state.'
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
        DiskPath = $disk
        DiskBytes = $diskBytes
        RunArtifactDirectory = if ($null -eq $runArtifact) { $null } else { $runArtifact.FullName }
        PlanPath = $planPath
    }
    Write-Utf8NoBom (Join-Path $suiteDirectory 'suite-result.json') (($result | ConvertTo-Json -Depth 6) + "`n")
}
if (-not $passed) {
    Write-Error "Format/mount/remount suite failed: $failure Artifacts: '$suiteDirectory'."
    exit 1
}
Write-Output "Format/mount/remount suite passed for $diskBytes bytes. Artifacts: '$suiteDirectory'."
