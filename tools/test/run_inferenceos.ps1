[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EspPath,
    [Parameter(Mandatory)][string]$PersistentDiskPath,
    [string]$OvmfCodePath = $env:INFERENCEOS_OVMF_CODE,
    [string]$OvmfVarsPath = $env:INFERENCEOS_OVMF_VARS,
    [string]$QemuPath,
    [string]$WorkDirectory,
    [string]$SerialLogPath,
    [ValidateRange(128, 16384)][uint32]$MemoryMiB = 512,
    [bool]$Headless = $true,
    [switch]$PreserveFirmwareVariables,
    [switch]$SkipVersionCheck,
    [switch]$DryRun,
    [string[]]$ExtraQemuArgument = @()
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RequiredQemuVersion = '11.1.0'
$SerialMarkers = [ordered]@{
    EarlySerialReady = 'INFERENCEOS:EARLY_SERIAL_READY'
    CuiReady = 'INFERENCEOS:CUI_READY'
    GuiReady = 'INFERENCEOS:GUI_READY'
    GuiUnavailable = 'INFERENCEOS:GUI_UNAVAILABLE'
    CuiRecoveryReady = 'INFERENCEOS:CUI_RECOVERY_READY'
    Panic = 'INFERENCEOS:PANIC'
    PanicHalt = 'INFERENCEOS:PANIC_HALT'
    Shutdown = 'INFERENCEOS:SHUTDOWN'
}

function Resolve-RequiredFile([string]$Path, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Description path is required. Pass it explicitly or source the bootstrap environment."
    }
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not [System.IO.File]::Exists($resolved)) {
        throw "$Description '$resolved' does not exist."
    }
    return $resolved
}

function Resolve-QemuExecutable([string]$RequestedPath) {
    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return Resolve-RequiredFile $RequestedPath 'QEMU executable'
    }
    if (-not [string]::IsNullOrWhiteSpace($env:INFERENCEOS_TOOL_ROOT)) {
        $candidateName = if (
            [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
        ) { 'qemu-system-x86_64.exe' } else { 'qemu-system-x86_64' }
        $candidate = Join-Path $env:INFERENCEOS_TOOL_ROOT "bin/$candidateName"
        if ([System.IO.File]::Exists($candidate)) { return [System.IO.Path]::GetFullPath($candidate) }
    }
    $command = Get-Command qemu-system-x86_64 -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $command) {
        throw 'qemu-system-x86_64 was not found; run the bootstrap or pass -QemuPath.'
    }
    return $command.Source
}

function Convert-ToQemuOptionPath([string]$Path) {
    # QEMU key/value options use a doubled comma as their literal-comma escape.
    return $Path.Replace(',', ',,')
}

$esp = Resolve-RequiredFile $EspPath 'ESP image'
$persistentDisk = Resolve-RequiredFile $PersistentDiskPath 'Persistent disk'
$ovmfCode = Resolve-RequiredFile $OvmfCodePath 'OVMF code image'
$ovmfVarsTemplate = Resolve-RequiredFile $OvmfVarsPath 'OVMF variables template'
$qemu = Resolve-QemuExecutable $QemuPath

if ($esp -eq $persistentDisk -or $ovmfCode -eq $ovmfVarsTemplate) {
    throw 'ESP, persistent disk, OVMF code, and OVMF variables must be distinct files.'
}
if ((Get-Item -LiteralPath $persistentDisk).Length -lt 50000000000) {
    throw 'The persistent disk is smaller than the required 50,000,000,000 bytes.'
}
if (-not $SkipVersionCheck) {
    $versionOutput = @(& $qemu --version 2>&1)
    if ($LASTEXITCODE -ne 0 -or ($versionOutput -join "`n") -notmatch [regex]::Escape($RequiredQemuVersion)) {
        throw "InferenceOS requires QEMU $RequiredQemuVersion; found '$($versionOutput -join ' ')'."
    }
}

$work = if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
    Join-Path (Split-Path -Parent $esp) 'run'
} else {
    [System.IO.Path]::GetFullPath($WorkDirectory)
}
[System.IO.Directory]::CreateDirectory($work) | Out-Null
$runtimeVars = Join-Path $work 'OVMF_VARS.fd'
if (-not $PreserveFirmwareVariables -or -not [System.IO.File]::Exists($runtimeVars)) {
    [System.IO.File]::Copy($ovmfVarsTemplate, $runtimeVars, $true)
}
if ((Get-Item -LiteralPath $runtimeVars).Length -ne (Get-Item -LiteralPath $ovmfVarsTemplate).Length) {
    throw 'Runtime OVMF variable-store copy has an unexpected size.'
}

$arguments = [System.Collections.Generic.List[string]]::new()
$arguments.AddRange([string[]]@(
    '-machine', 'q35,accel=tcg',
    '-cpu', 'qemu64',
    '-smp', '1',
    '-m', "${MemoryMiB}M",
    '-nodefaults',
    '-no-reboot',
    '-no-shutdown',
    '-rtc', 'base=utc,clock=vm',
    '-monitor', 'none',
    '-drive', "if=pflash,format=raw,readonly=on,file=$(Convert-ToQemuOptionPath $ovmfCode)",
    '-drive', "if=pflash,format=raw,file=$(Convert-ToQemuOptionPath $runtimeVars)",
    # QEMU 11.1 rejects a read-only block node behind ide-hd. The ESP is a
    # disposable build artifact; firmware receives it as a writable IDE disk.
    '-drive', "if=none,id=esp,format=raw,file=$(Convert-ToQemuOptionPath $esp)",
    '-device', 'ide-hd,drive=esp,bus=ide.0',
    '-drive', "if=none,id=persistent,format=raw,cache=writeback,file=$(Convert-ToQemuOptionPath $persistentDisk)",
    '-device', 'virtio-blk-pci,drive=persistent',
    '-device', 'VGA'
))
if ($Headless) {
    $arguments.AddRange([string[]]@('-display', 'none'))
}
if ([string]::IsNullOrWhiteSpace($SerialLogPath)) {
    $arguments.AddRange([string[]]@('-serial', 'stdio'))
} else {
    $serialLog = [System.IO.Path]::GetFullPath($SerialLogPath)
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $serialLog)) | Out-Null
    $arguments.AddRange([string[]]@('-serial', "file:$(Convert-ToQemuOptionPath $serialLog)"))
}
$arguments.AddRange([string[]]$ExtraQemuArgument)

$profile = [pscustomobject][ordered]@{
    SchemaVersion = 1
    Machine = 'q35'
    Accelerator = 'tcg'
    Cpu = 'qemu64'
    CpuCount = 1
    MemoryMiB = $MemoryMiB
    QemuPath = $qemu
    RequiredQemuVersion = $RequiredQemuVersion
    OvmfCodePath = $ovmfCode
    OvmfVariablesPath = $runtimeVars
    EspPath = $esp
    PersistentDiskPath = $persistentDisk
    Headless = $Headless
    SerialLogPath = if ([string]::IsNullOrWhiteSpace($SerialLogPath)) { $null } else { $serialLog }
    SerialMarkers = [pscustomobject]$SerialMarkers
    Arguments = [string[]]$arguments
}

if ($DryRun) {
    $profile
    exit 0
}

Write-Host "Starting InferenceOS with QEMU $RequiredQemuVersion (q35/TCG, one CPU)."
Write-Host "Early-ready marker: $($SerialMarkers.EarlySerialReady)"
Write-Host "CUI-ready marker:   $($SerialMarkers.CuiReady)"
& $qemu @arguments
exit $LASTEXITCODE
