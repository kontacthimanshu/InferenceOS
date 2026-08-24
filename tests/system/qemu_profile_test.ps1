[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '../..'),
    [string]$ArtifactDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Quote-PowerShellLiteral([string]$Value) {
    return "'$($Value.Replace("'", "''"))'"
}

function Invoke-Profile([hashtable]$Parameters) {
    $tokens = [System.Collections.Generic.List[string]]::new()
    $tokens.Add("& $(Quote-PowerShellLiteral $launcher)")
    foreach ($name in ($Parameters.Keys | Sort-Object)) {
        $value = $Parameters[$name]
        if ($value -is [bool]) {
            $tokens.Add("-$name`:`$$($value.ToString().ToLowerInvariant())")
        } elseif ($value -is [System.Management.Automation.SwitchParameter]) {
            if ($value.IsPresent) { $tokens.Add("-$name") }
        } else {
            $tokens.Add("-$name $(Quote-PowerShellLiteral ([string]$value))")
        }
    }
    $tokens.Add('-DryRun | ConvertTo-Json -Depth 8 -Compress')
    $command = $tokens -join ' '
    $encoded = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($command))
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Process -Id $PID).Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    if ($null -ne $startInfo.PSObject.Properties['ArgumentList']) {
        foreach ($argument in @('-NoProfile', '-EncodedCommand', $encoded)) {
            [void]$startInfo.ArgumentList.Add($argument)
        }
    } else {
        $startInfo.Arguments = "-NoProfile -EncodedCommand $encoded"
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'Unable to start the profile-test PowerShell process.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $standardOutput = $stdout.GetAwaiter().GetResult()
        $standardError = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) {
            throw "QEMU profile process failed with code $($process.ExitCode): $standardError"
        }
        return $standardOutput | ConvertFrom-Json
    } finally {
        $process.Dispose()
    }
}

function Assert-ArgumentPair([string[]]$Arguments, [string]$Option, [string]$Value) {
    for ($index = 0; $index + 1 -lt $Arguments.Count; ++$index) {
        if ($Arguments[$index] -ceq $Option -and $Arguments[$index + 1] -ceq $Value) { return }
    }
    throw "QEMU arguments do not contain the exact pair '$Option' '$Value'."
}

$root = [System.IO.Path]::GetFullPath($RepositoryRoot)
$launcher = Join-Path $root 'tools/test/run_inferenceos.ps1'
if (-not [System.IO.File]::Exists($launcher)) { throw "QEMU launcher '$launcher' is missing." }
$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $root 'build/system/qemu-profile'
} else {
    [System.IO.Path]::GetFullPath($ArtifactDirectory)
}
$caseRoot = Join-Path $artifactRoot "host path,case-$PID"
[System.IO.Directory]::CreateDirectory($caseRoot) | Out-Null
$esp = Join-Path $caseRoot 'esp,image.img'
$disk = Join-Path $caseRoot 'persistent,disk.raw'
$ovmfCode = Join-Path $caseRoot 'OVMF CODE.fd'
$ovmfVars = Join-Path $caseRoot 'OVMF VARS.fd'
$serial = Join-Path $caseRoot 'serial output.log'
[System.IO.File]::WriteAllBytes($esp, [byte[]](1,2,3,4))
[System.IO.File]::WriteAllBytes($ovmfCode, [byte[]](5,6,7,8))
[System.IO.File]::WriteAllBytes($ovmfVars, [byte[]](9,10,11,12))
$diskStream = [System.IO.File]::Open($disk, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
try { $diskStream.SetLength(50000000000) } finally { $diskStream.Dispose() }

$common = @{
    EspPath = $esp
    PersistentDiskPath = $disk
    OvmfCodePath = $ovmfCode
    OvmfVarsPath = $ovmfVars
    QemuPath = (Get-Process -Id $PID).Path
    SkipVersionCheck = [System.Management.Automation.SwitchParameter]::new($true)
}
$headlessWork = Join-Path $caseRoot 'headless work'
$headlessParameters = $common.Clone()
$headlessParameters.WorkDirectory = $headlessWork
$headlessParameters.SerialLogPath = $serial
$headlessParameters.MemoryMiB = 768
$headless = Invoke-Profile $headlessParameters
if ($headless.SchemaVersion -ne 1 -or $headless.Machine -cne 'q35' -or
    $headless.Accelerator -cne 'tcg' -or $headless.Cpu -cne 'qemu64' -or
    $headless.CpuCount -ne 1 -or $headless.MemoryMiB -ne 768 -or -not $headless.Headless -or
    $headless.RequiredQemuVersion -cne '11.1.0') {
    throw 'The headless foundational profile does not match the q35/TCG/one-CPU contract.'
}
$headlessArguments = [string[]]$headless.Arguments
Assert-ArgumentPair $headlessArguments '-machine' 'q35,accel=tcg'
Assert-ArgumentPair $headlessArguments '-cpu' 'qemu64'
Assert-ArgumentPair $headlessArguments '-smp' '1'
Assert-ArgumentPair $headlessArguments '-m' '768M'
Assert-ArgumentPair $headlessArguments '-display' 'none'
Assert-ArgumentPair $headlessArguments '-serial' "file:$($serial.Replace(',', ',,'))"
Assert-ArgumentPair $headlessArguments '-drive' "if=none,id=esp,format=raw,file=$($esp.Replace(',', ',,'))"
Assert-ArgumentPair $headlessArguments '-drive' "if=none,id=persistent,format=raw,cache=writeback,file=$($disk.Replace(',', ',,'))"
foreach ($marker in @('EarlySerialReady','CuiReady','GuiReady','GuiUnavailable','CuiRecoveryReady','Panic','PanicHalt','Shutdown')) {
    if ($null -eq $headless.SerialMarkers.PSObject.Properties[$marker]) {
        throw "The portable profile omits serial marker '$marker'."
    }
}

$runtimeVars = Join-Path $headlessWork 'OVMF_VARS.fd'
if (-not [System.IO.File]::Exists($runtimeVars) -or
    -not [System.Linq.Enumerable]::SequenceEqual([System.IO.File]::ReadAllBytes($runtimeVars), [byte[]](9,10,11,12))) {
    throw 'OVMF variable template was not copied byte-for-byte.'
}
[System.IO.File]::WriteAllBytes($runtimeVars, [byte[]](42,42,42,42))
$preserveParameters = $headlessParameters.Clone()
$preserveParameters.PreserveFirmwareVariables = [System.Management.Automation.SwitchParameter]::new($true)
[void](Invoke-Profile $preserveParameters)
if ([System.IO.File]::ReadAllBytes($runtimeVars)[0] -ne 42) {
    throw 'PreserveFirmwareVariables did not retain the runtime OVMF variable store.'
}
[void](Invoke-Profile $headlessParameters)
if ([System.IO.File]::ReadAllBytes($runtimeVars)[0] -ne 9) {
    throw 'A normal launch did not reset the runtime OVMF variable store from its template.'
}

$visibleParameters = $common.Clone()
$visibleParameters.WorkDirectory = Join-Path $caseRoot 'visible work'
$visibleParameters.Headless = $false
$visible = Invoke-Profile $visibleParameters
$visibleArguments = [string[]]$visible.Arguments
if ($visible.Headless -or '-display' -in $visibleArguments) {
    throw 'The visible profile unexpectedly retained headless display arguments.'
}
Assert-ArgumentPair $visibleArguments '-serial' 'stdio'

$report = [ordered]@{
    schema_version = 1
    passed = $true
    contract = 'q35-ovmf-tcg-host-portability'
    host = [ordered]@{
        platform = [System.Environment]::OSVersion.Platform.ToString()
        powershell_edition = $PSVersionTable.PSEdition
        powershell_version = $PSVersionTable.PSVersion.ToString()
        directory_separator = [string][System.IO.Path]::DirectorySeparatorChar
    }
    supported_hosts = @('Windows PowerShell 5.1+', 'PowerShell 7+ on Windows, Linux, or macOS')
    invariants = @(
        'q35 machine with TCG acceleration', 'qemu64 CPU with one vCPU',
        'separate read-only OVMF code and copied writable variables',
        'raw ESP plus raw writeback virtio-blk persistent disk',
        'headless serial-file and visible serial-stdio modes',
        'QEMU key/value path commas escaped by doubling'
    )
    cases = @(
        [ordered]@{ name = 'headless-path-portability'; passed = $true },
        [ordered]@{ name = 'ovmf-variable-lifecycle'; passed = $true },
        [ordered]@{ name = 'visible-console'; passed = $true }
    )
}
$reportPath = Join-Path $caseRoot 'qemu-profile-report.json'
Write-Utf8NoBom $reportPath (($report | ConvertTo-Json -Depth 8).Replace("`r`n", "`n") + "`n")
[pscustomobject]@{ Passed = $true; ReportPath = $reportPath; Host = $report.host }
