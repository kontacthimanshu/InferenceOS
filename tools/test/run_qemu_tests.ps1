[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EspPath,
    [Parameter(Mandatory)][string]$PersistentDiskPath,
    [string]$OvmfCodePath = $env:INFERENCEOS_OVMF_CODE,
    [string]$OvmfVarsPath = $env:INFERENCEOS_OVMF_VARS,
    [string]$QemuPath,
    [string]$ArtifactDirectory,
    [string]$RunName = 'foundational-cui',
    [ValidateRange(1, 3600)][uint32]$TimeoutSeconds = 60,
    [string[]]$RequiredMarker = @('INFERENCEOS:CUI_READY'),
    [string[]]$ForbiddenMarker = @(),
    [switch]$RetainSuccessfulArtifacts,
    [switch]$SkipVersionCheck,
    [switch]$DryRun,
    [switch]$ReleaseMatrix,
    [string]$TestAction,
    [string]$TestArgument,
    [ValidateRange(1, 4294967295)][uint32]$TestSequence = 1,
    [string[]]$ExtraQemuArgument = @(),
    [string]$ExtraQemuArgumentJson
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$PanicMarkers = @('INFERENCEOS:PANIC', 'INFERENCEOS:PANIC_HALT')
$scriptRoot = Split-Path -Parent $PSCommandPath
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '../..'))
$launcher = Join-Path $scriptRoot 'run_inferenceos.ps1'
$timeoutWasSpecified = $PSBoundParameters.ContainsKey('TimeoutSeconds')

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Convert-ToNativeArgumentString([string[]]$Arguments) {
    $quoted = foreach ($argument in $Arguments) {
        if ($argument.Length -gt 0 -and $argument -notmatch '[\s"]') {
            $argument
            continue
        }
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

function Read-AvailableText([string]$Path) {
    if (-not [System.IO.File]::Exists($Path)) { return '' }
    try {
        $stream = [System.IO.File]::Open(
            $Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete
        )
        try {
            $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8, $true)
            try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
    } catch {
        return ''
    }
}

function Get-FreeTcpPort {
    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback, 0
    )
    try {
        $listener.Start()
        return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally {
        $listener.Stop()
    }
}

function Send-TestControlRequest(
    [int]$Port, [uint32]$Sequence, [string]$Action, [string]$Argument
) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    $lastFailure = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $pending = $client.BeginConnect('127.0.0.1', $Port, $null, $null)
            if (-not $pending.AsyncWaitHandle.WaitOne(500)) {
                throw "Connection attempt to q35 COM2 timed out."
            }
            $client.EndConnect($pending)
            $line = "INFERENCEOS_TEST 1 $Sequence $Action"
            if (-not [string]::IsNullOrWhiteSpace($Argument)) { $line += " $Argument" }
            $bytes = [System.Text.Encoding]::ASCII.GetBytes($line + "`n")
            $stream = $client.GetStream()
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush()
            return
        } catch {
            $lastFailure = $_.Exception.Message
            Start-Sleep -Milliseconds 50
        } finally {
            $client.Dispose()
        }
    }
    throw "Could not connect to the q35 COM2 test-control port $Port within 5 seconds: $lastFailure"
}

function Get-PortableRelativePath([string]$BasePath, [string]$TargetPath) {
    $base = [System.IO.Path]::GetFullPath($BasePath).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    $target = [System.IO.Path]::GetFullPath($TargetPath)
    $relative = ([uri]$base).MakeRelativeUri([uri]$target).ToString()
    return [uri]::UnescapeDataString($relative).Replace('\', '/')
}

function Invoke-PowerShellScript([string]$Path, [string[]]$Arguments, [string]$StandardOutputPath, [string]$StandardErrorPath) {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Process -Id $PID).Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $nativeArguments = @('-NoProfile', '-File', $Path) + $Arguments
    if ($null -ne $startInfo.PSObject.Properties['ArgumentList']) {
        foreach ($argument in $nativeArguments) { [void]$startInfo.ArgumentList.Add($argument) }
    } else {
        $startInfo.Arguments = Convert-ToNativeArgumentString $nativeArguments
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw "PowerShell did not start release suite '$Path'." }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        Write-Utf8NoBom $StandardOutputPath $stdoutTask.GetAwaiter().GetResult()
        Write-Utf8NoBom $StandardErrorPath $stderrTask.GetAwaiter().GetResult()
        return $process.ExitCode
    } finally {
        $process.Dispose()
    }
}

function Get-EvidenceFiles([string]$Root, [string]$ManifestPath) {
    $manifestFullPath = [System.IO.Path]::GetFullPath($ManifestPath)
    $records = foreach ($file in Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object FullName) {
        if ($file.FullName -eq $manifestFullPath) { continue }
        $relativePath = Get-PortableRelativePath $Root $file.FullName
        [ordered]@{
            Path = $relativePath
            Bytes = [uint64]$file.Length
            Sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    return @($records)
}

function Invoke-ReleaseMatrix {
    $artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
        Join-Path $repositoryRoot 'build/qemu-tests'
    } else {
        [System.IO.Path]::GetFullPath($ArtifactDirectory)
    }
    $matrixId = "release-matrix-$([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))-$PID"
    $matrixDirectory = Join-Path $artifactRoot $matrixId
    [System.IO.Directory]::CreateDirectory($matrixDirectory) | Out-Null
    $manifestPath = Join-Path $matrixDirectory 'evidence-manifest.json'
    $started = [DateTime]::UtcNow
    $suites = @(
        [ordered]@{ Name = 'boot-gui-recovery'; Script = 'boot_gui_test.ps1'; Timeout = 60; Arguments = @('-RunCount', '20') },
        [ordered]@{ Name = 'format-mount'; Script = 'format_mount_test.ps1'; Timeout = 120; Arguments = @() },
        [ordered]@{ Name = 'file-explorer'; Script = 'file_explorer_test.ps1'; Timeout = 60; Arguments = @() },
        [ordered]@{ Name = 'reboot-persistence'; Script = 'reboot_persistence_test.ps1'; Timeout = 120; Arguments = @('-CycleCount', '20') },
        [ordered]@{ Name = 'directory-interop'; Script = 'directory_interop_test.ps1'; Timeout = 120; Arguments = @() },
        [ordered]@{ Name = 'display-safe-file-commands'; Script = 'display_safe_file_commands_test.ps1'; Timeout = 120; Arguments = @() },
        [ordered]@{ Name = 'fault-matrix'; Script = 'fault_matrix_test.ps1'; Timeout = 120; Arguments = @() },
        [ordered]@{ Name = 'registry-benchmark'; Script = 'registry_benchmark_qemu_test.ps1'; Timeout = 120; Arguments = @() }
    )
    $results = [System.Collections.Generic.List[object]]::new()
    $matrixPassed = $true
    foreach ($suite in $suites) {
        $suiteStarted = [DateTime]::UtcNow
        $suiteArtifactRoot = Join-Path $matrixDirectory $suite.Name
        [System.IO.Directory]::CreateDirectory($suiteArtifactRoot) | Out-Null
        $stdoutPath = Join-Path $suiteArtifactRoot 'suite.stdout.log'
        $stderrPath = Join-Path $suiteArtifactRoot 'suite.stderr.log'
        $scriptPath = Join-Path $repositoryRoot "tests/system/$($suite.Script)"
        if (-not [System.IO.File]::Exists($scriptPath)) { throw "Release suite '$scriptPath' is missing." }
        $suiteTimeout = if ($timeoutWasSpecified) { $TimeoutSeconds } else { $suite.Timeout }
        $arguments = @(
            '-EspPath', [System.IO.Path]::GetFullPath($EspPath),
            '-PersistentDiskPath', [System.IO.Path]::GetFullPath($PersistentDiskPath),
            '-ArtifactDirectory', $suiteArtifactRoot,
            '-TimeoutSeconds', [string]$suiteTimeout
        ) + [string[]]$suite.Arguments
        if (-not [string]::IsNullOrWhiteSpace($OvmfCodePath)) { $arguments += @('-OvmfCodePath', $OvmfCodePath) }
        if (-not [string]::IsNullOrWhiteSpace($OvmfVarsPath)) { $arguments += @('-OvmfVarsPath', $OvmfVarsPath) }
        if (-not [string]::IsNullOrWhiteSpace($QemuPath)) { $arguments += @('-QemuPath', $QemuPath) }
        if ($SkipVersionCheck) { $arguments += '-SkipVersionCheck' }
        if ($DryRun) { $arguments += '-DryRun' }
        $exitCode = Invoke-PowerShellScript $scriptPath $arguments $stdoutPath $stderrPath
        $passed = $exitCode -eq 0
        if (-not $passed) { $matrixPassed = $false }
        $suiteDirectory = Get-ChildItem -LiteralPath $suiteArtifactRoot -Directory |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        $results.Add([pscustomobject][ordered]@{
            Name = $suite.Name
            Script = Get-PortableRelativePath $repositoryRoot $scriptPath
            Arguments = [string[]]$arguments
            Status = if ($DryRun -and $passed) { 'planned' } elseif ($passed) { 'passed' } else { 'failed' }
            ExitCode = $exitCode
            StartedUtc = $suiteStarted.ToString('o')
            FinishedUtc = [DateTime]::UtcNow.ToString('o')
            DurationMilliseconds = [Math]::Round(([DateTime]::UtcNow - $suiteStarted).TotalMilliseconds)
            ArtifactDirectory = if ($null -eq $suiteDirectory) { $null } else {
                Get-PortableRelativePath $matrixDirectory $suiteDirectory.FullName
            }
            StandardOutput = Get-PortableRelativePath $matrixDirectory $stdoutPath
            StandardError = Get-PortableRelativePath $matrixDirectory $stderrPath
        })
    }
    $inputEvidence = foreach ($input in @(
        [ordered]@{ Role = 'esp'; Path = [System.IO.Path]::GetFullPath($EspPath) },
        [ordered]@{ Role = 'persistent-disk'; Path = [System.IO.Path]::GetFullPath($PersistentDiskPath) }
    )) {
        $item = Get-Item -LiteralPath $input.Path
        [ordered]@{ Role = $input.Role; Path = $item.FullName; Bytes = [uint64]$item.Length }
    }
    $manifest = [ordered]@{
        SchemaVersion = 1
        MatrixId = $matrixId
        Matrix = 'primary-toolchain-qemu-release'
        DryRun = [bool]$DryRun
        Passed = $matrixPassed
        StartedUtc = $started.ToString('o')
        FinishedUtc = [DateTime]::UtcNow.ToString('o')
        Inputs = @($inputEvidence)
        Profile = [ordered]@{ Machine = 'q35'; Accelerator = 'tcg'; CpuCount = 1; RequiredQemuVersion = '11.1.0' }
        Suites = @($results)
        EvidenceFiles = @()
    }
    Write-Utf8NoBom $manifestPath (($manifest | ConvertTo-Json -Depth 10) + "`n")
    $manifest.EvidenceFiles = Get-EvidenceFiles $matrixDirectory $manifestPath
    Write-Utf8NoBom $manifestPath (($manifest | ConvertTo-Json -Depth 10) + "`n")
    [pscustomobject]@{ MatrixDirectory = $matrixDirectory; EvidenceManifest = $manifestPath; Passed = $matrixPassed }
    if (-not $matrixPassed) { exit 1 }
    exit 0
}

if ($ReleaseMatrix) {
    Invoke-ReleaseMatrix
}

if (-not [System.IO.File]::Exists($launcher)) {
    throw "InferenceOS launcher '$launcher' is missing."
}
if (-not [string]::IsNullOrWhiteSpace($ExtraQemuArgumentJson)) {
    if ($ExtraQemuArgument.Count -gt 0) {
        throw 'Use either ExtraQemuArgument or ExtraQemuArgumentJson, not both.'
    }
    try {
        $decodedValue = ConvertFrom-Json -InputObject $ExtraQemuArgumentJson
    } catch {
        throw "ExtraQemuArgumentJson is not a valid JSON string array: $($_.Exception.Message)"
    }
    if ($ExtraQemuArgumentJson.TrimStart()[0] -ne '[') {
        throw 'ExtraQemuArgumentJson must encode a JSON array.'
    }
    $decodedArguments = @($decodedValue)
    if ($decodedArguments | Where-Object { $_ -isnot [string] }) {
        throw 'ExtraQemuArgumentJson must contain only strings.'
    }
    $ExtraQemuArgument = [string[]]$decodedArguments
}
if ($RequiredMarker.Count -eq 0 -or ($RequiredMarker | Where-Object { [string]::IsNullOrWhiteSpace($_) })) {
    throw 'At least one nonempty required marker is needed.'
}
if ($ForbiddenMarker | Where-Object { [string]::IsNullOrWhiteSpace($_) }) {
    throw 'Forbidden markers must be nonempty.'
}
if (-not [string]::IsNullOrWhiteSpace($TestAction) -and
    $TestAction -notmatch '^[a-z][a-z0-9_]{0,46}$') {
    throw 'TestAction must be a lowercase protocol action token of at most 47 characters.'
}
if (-not [string]::IsNullOrEmpty($TestArgument) -and
    ($TestArgument.Length -ge 160 -or $TestArgument -match '[^\x20-\x7E]')) {
    throw 'TestArgument must be printable ASCII and fit the bounded one-line protocol payload.'
}
$allForbiddenMarkers = @($PanicMarkers + $ForbiddenMarker | Select-Object -Unique)
if (-not [string]::IsNullOrWhiteSpace($TestAction)) {
    $allForbiddenMarkers = @($allForbiddenMarkers + 'INFERENCEOS:TEST_CONTROL_FAIL' |
        Select-Object -Unique)
}
$testPassMarker = if ([string]::IsNullOrWhiteSpace($TestAction)) { $null } else {
    "INFERENCEOS:TEST_CONTROL_PASS version=1 sequence=$TestSequence action=$TestAction"
}
$testBeginMarker = if ([string]::IsNullOrWhiteSpace($TestAction)) { $null } else {
    "INFERENCEOS:TEST_CONTROL_BEGIN version=1 sequence=$TestSequence action=$TestAction"
}
$effectiveRequiredMarkers = @($RequiredMarker)
if ($null -ne $testBeginMarker) { $effectiveRequiredMarkers += $testBeginMarker }
if ($null -ne $testPassMarker) { $effectiveRequiredMarkers += $testPassMarker }
foreach ($marker in $effectiveRequiredMarkers) {
    if ($marker -in $allForbiddenMarkers) { throw "Marker '$marker' cannot be both required and forbidden." }
}

$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $repositoryRoot 'build/qemu-tests'
} else {
    [System.IO.Path]::GetFullPath($ArtifactDirectory)
}
$safeRunName = $RunName -replace '[^A-Za-z0-9_.-]', '_'
if ([string]::IsNullOrWhiteSpace($safeRunName)) { throw 'RunName must contain a portable filename character.' }
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$runDirectory = Join-Path $artifactRoot "$safeRunName-$stamp-$PID"
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$serialLog = Join-Path $runDirectory 'serial.log'
$qemuStdout = Join-Path $runDirectory 'qemu.stdout.log'
$qemuStderr = Join-Path $runDirectory 'qemu.stderr.log'
$launchManifest = Join-Path $runDirectory 'launch.json'
$resultManifest = Join-Path $runDirectory 'result.json'
$testControlPort = if ([string]::IsNullOrWhiteSpace($TestAction)) { 0 } else { Get-FreeTcpPort }

$launcherParameters = @{
    EspPath = $EspPath
    PersistentDiskPath = $PersistentDiskPath
    OvmfCodePath = $OvmfCodePath
    OvmfVarsPath = $OvmfVarsPath
    QemuPath = $QemuPath
    WorkDirectory = $runDirectory
    SerialLogPath = $serialLog
    Headless = $true
    DryRun = $true
    TestControlPort = $testControlPort
    ExtraQemuArgument = $ExtraQemuArgument
}
if ($SkipVersionCheck) { $launcherParameters.SkipVersionCheck = $true }
$profile = & $launcher @launcherParameters
$launchRecord = [ordered]@{
    SchemaVersion = 1
    RunName = $RunName
    StartedUtc = [DateTime]::UtcNow.ToString('o')
    TimeoutSeconds = $TimeoutSeconds
    RequiredMarkers = $effectiveRequiredMarkers
    ForbiddenMarkers = $allForbiddenMarkers
    TestControlRequest = if ($testControlPort -eq 0) { $null } else {
        [ordered]@{
            ProtocolVersion = 1
            Sequence = $TestSequence
            Action = $TestAction
            Argument = $TestArgument
        }
    }
    Profile = $profile
}
Write-Utf8NoBom $launchManifest (($launchRecord | ConvertTo-Json -Depth 8) + "`n")

if ($DryRun) {
    [pscustomobject]@{ RunDirectory = $runDirectory; LaunchManifest = $launchManifest; Profile = $profile }
    exit 0
}

$process = $null
$stdoutTask = $null
$stderrTask = $null
$passed = $false
$timedOut = $false
$failure = $null
$matchedMarkers = [System.Collections.Generic.List[string]]::new()
$started = [DateTime]::UtcNow
$testRequestSent = $false
try {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $profile.QemuExecutionPath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    if ($null -ne $startInfo.PSObject.Properties['ArgumentList']) {
        foreach ($argument in @($profile.QemuExecutionPrefix) + @($profile.Arguments)) {
            [void]$startInfo.ArgumentList.Add($argument)
        }
    } else {
        $startInfo.Arguments = Convert-ToNativeArgumentString (
            @($profile.QemuExecutionPrefix) + @($profile.Arguments)
        )
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw 'QEMU process did not start.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)

    while ([DateTime]::UtcNow -lt $deadline) {
        $transcript = Read-AvailableText $serialLog
        if (-not $testRequestSent -and $testControlPort -ne 0 -and
            $transcript.Contains('INFERENCEOS:TEST_CONTROL_READY version=1')) {
            Send-TestControlRequest $testControlPort $TestSequence $TestAction $TestArgument
            $testRequestSent = $true
        }
        foreach ($marker in $allForbiddenMarkers) {
            if ($transcript.Contains($marker)) { throw "Forbidden serial marker observed: $marker" }
        }
        $matchedMarkers.Clear()
        foreach ($marker in $effectiveRequiredMarkers) {
            if ($transcript.Contains($marker)) { $matchedMarkers.Add($marker) }
        }
        if ($matchedMarkers.Count -eq $effectiveRequiredMarkers.Count) { $passed = $true; break }
        if ($process.HasExited) {
            throw "QEMU exited with code $($process.ExitCode) before required markers appeared."
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $passed) { $timedOut = $true; throw "QEMU story test timed out after $TimeoutSeconds seconds." }
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        try { $process.Kill() } catch { }
        [void]$process.WaitForExit(5000)
    }
    if ($null -ne $process) {
        if ($null -ne $stdoutTask) {
            try { Write-Utf8NoBom $qemuStdout $stdoutTask.GetAwaiter().GetResult() } catch { }
        }
        if ($null -ne $stderrTask) {
            try { Write-Utf8NoBom $qemuStderr $stderrTask.GetAwaiter().GetResult() } catch { }
        }
        $process.Dispose()
    }
    $result = [ordered]@{
        SchemaVersion = 1
        RunName = $RunName
        Passed = $passed
        TimedOut = $timedOut
        Failure = $failure
        StartedUtc = $started.ToString('o')
        FinishedUtc = [DateTime]::UtcNow.ToString('o')
        DurationMilliseconds = [Math]::Round(([DateTime]::UtcNow - $started).TotalMilliseconds)
        MatchedMarkers = [string[]]$matchedMarkers
        TestControlRequestSent = $testRequestSent
        SerialLog = $serialLog
        QemuStandardOutput = $qemuStdout
        QemuStandardError = $qemuStderr
    }
    Write-Utf8NoBom $resultManifest (($result | ConvertTo-Json -Depth 5) + "`n")
}

if (-not $passed) {
    Write-Error "QEMU story test failed: $failure Artifacts retained at '$runDirectory'."
    exit 1
}
if ($RetainSuccessfulArtifacts) {
    Write-Output "QEMU story test passed. Artifacts retained at '$runDirectory'."
} else {
    [System.IO.Directory]::Delete($runDirectory, $true)
    Write-Output 'QEMU story test passed.'
}
