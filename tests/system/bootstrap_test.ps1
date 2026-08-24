[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..\..'),
    [string]$ArtifactDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Convert-ToBashPath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) { return $full }
    $converted = & wsl.exe --exec wslpath -a -u $full
    if ($LASTEXITCODE -ne 0) { throw "Unable to convert '$full' to a WSL path." }
    return $converted.Trim()
}

function Quote-Bash([string]$Value) {
    if ($Value.IndexOf("'", [StringComparison]::Ordinal) -ge 0) {
        throw "Test fixture path contains an unsupported apostrophe: $Value"
    }
    return "'$Value'"
}

function Invoke-Bash([string]$Command, [string]$HomePath) {
    $commandWithHome = "export HOME=$(Quote-Bash $HomePath); $Command"
    $output = if ([System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT) {
        & wsl.exe --exec bash -lc $commandWithHome 2>&1
    } else {
        & bash -lc $commandWithHome 2>&1
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Bash failed ($LASTEXITCODE): $($output -join "`n")"
    }
    return $output -join "`n"
}

function Get-TreeDigest([string]$Root) {
    $prefix = [System.IO.Path]::GetFullPath($Root).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    $records = foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File | Sort-Object FullName) {
        if (-not $file.FullName.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "File '$($file.FullName)' escaped fixture root '$Root'."
        }
        $relative = $file.FullName.Substring($prefix.Length).Replace('\', '/')
        "$relative|$($file.Length)|$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash)"
    }
    $bytes = [System.Text.Encoding]::UTF8.GetBytes(($records -join "`n"))
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}

$root = [System.IO.Path]::GetFullPath($RepositoryRoot)
$bootstrap = Join-Path $root 'tools/bootstrap/wsl-ubuntu.sh'
$versionsPath = Join-Path $root 'tools/bootstrap/versions.json'
if (-not (Test-Path -LiteralPath $bootstrap) -or -not (Test-Path -LiteralPath $versionsPath)) {
    throw 'Bootstrap script or pinned version manifest is missing.'
}
$versions = Get-Content -Raw -LiteralPath $versionsPath | ConvertFrom-Json
$expected = [ordered]@{
    binutils = '2.45'; gcc = '16.2.0'; llvm = '22.1.8'; qemu = '11.1.0'
    ovmf = 'edk2-stable202605'; cmake = '4.1.1'; ninja = '1.13.1'
}
if ($versions.schema_version -ne 1) { throw 'Tool version manifest schema must be 1.' }
foreach ($component in $expected.Keys) {
    if ($versions.components.$component.version -cne $expected[$component]) {
        throw "Pinned $component version differs from the approved research matrix."
    }
}

$scriptText = Get-Content -Raw -LiteralPath $bootstrap
foreach ($profileName in @('.profile', '.bashrc', '.bash_profile', '.zshrc')) {
    if ($scriptText.IndexOf($profileName, [StringComparison]::Ordinal) -ge 0) {
        throw "Bootstrap must not reference global profile '$profileName'."
    }
}

$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $root 'build/system/bootstrap-contract'
} else { [System.IO.Path]::GetFullPath($ArtifactDirectory) }
$caseRoot = Join-Path $artifactRoot "run-$PID"
$installRoot = Join-Path $caseRoot 'install'
$prefix = Join-Path $installRoot 'prefix'
$bin = Join-Path $prefix 'bin'
$download = Join-Path $caseRoot 'download'
$fixtureHome = Join-Path $caseRoot 'home'
$environmentFile = Join-Path $installRoot 'environment.sh'
[System.IO.Directory]::CreateDirectory($bin) | Out-Null
[System.IO.Directory]::CreateDirectory($download) | Out-Null
[System.IO.Directory]::CreateDirectory($fixtureHome) | Out-Null

$profiles = @('.profile', '.bashrc', '.bash_profile', '.zshrc')
foreach ($profile in $profiles) { Write-Utf8NoBom (Join-Path $fixtureHome $profile) "sentinel:$profile`n" }
$profileDigests = @{}
foreach ($profile in $profiles) {
    $profileDigests[$profile] = (Get-FileHash -LiteralPath (Join-Path $fixtureHome $profile) -Algorithm SHA256).Hash
}

$commands = [ordered]@{
    'x86_64-elf-as' = $expected.binutils
    'x86_64-elf-gcc' = $expected.gcc
    'clang' = $expected.llvm
    'ld.lld' = $expected.llvm
    'qemu-system-x86_64' = $expected.qemu
    'cmake' = $expected.cmake
    'ninja' = $expected.ninja
}
foreach ($command in $commands.Keys) {
    Write-Utf8NoBom (Join-Path $bin $command) "#!/usr/bin/env bash`nprintf '%s\n' '$($commands[$command])'`n"
}
Write-Utf8NoBom (Join-Path $bin 'sudo') "#!/usr/bin/env bash`nexit 0`n"
$ovmf = Join-Path $prefix 'share/ovmf'
[System.IO.Directory]::CreateDirectory($ovmf) | Out-Null
Write-Utf8NoBom (Join-Path $ovmf 'INFERENCEOS_OVMF_VERSION') "$($expected.ovmf)`n"
[System.IO.File]::WriteAllBytes((Join-Path $ovmf 'OVMF_CODE.fd'), [byte[]](1,2,3,4))
[System.IO.File]::WriteAllBytes((Join-Path $ovmf 'OVMF_VARS.fd'), [byte[]](5,6,7,8))

$bashBootstrap = Convert-ToBashPath $bootstrap
$bashInstall = Convert-ToBashPath $installRoot
$bashDownload = Convert-ToBashPath $download
$bashEnvironment = Convert-ToBashPath $environmentFile
$bashHome = Convert-ToBashPath $fixtureHome
$bashBin = Convert-ToBashPath $bin
$chmodTargets = @($commands.Keys | ForEach-Object { Quote-Bash "$bashBin/$_" }) + (Quote-Bash "$bashBin/sudo")
[void](Invoke-Bash ("chmod +x -- " + ($chmodTargets -join ' ')) $bashHome)
$invocation = "$(Quote-Bash $bashBootstrap) --install-root $(Quote-Bash $bashInstall) " +
    "--download-root $(Quote-Bash $bashDownload) --env-file $(Quote-Bash $bashEnvironment)"
[void](Invoke-Bash $invocation $bashHome)
$firstDigest = Get-TreeDigest $installRoot
[void](Invoke-Bash $invocation $bashHome)
$secondDigest = Get-TreeDigest $installRoot
if ($firstDigest -cne $secondDigest) { throw 'A repeated bootstrap changed the installed tool tree.' }
[void](Invoke-Bash ("$invocation --check") $bashHome)
foreach ($profile in $profiles) {
    $current = (Get-FileHash -LiteralPath (Join-Path $fixtureHome $profile) -Algorithm SHA256).Hash
    if ($current -cne $profileDigests[$profile]) { throw "Bootstrap modified '$profile'." }
}
$environment = Get-Content -Raw -LiteralPath $environmentFile
if ($environment.IndexOf('INFERENCEOS_TOOL_ROOT=', [StringComparison]::Ordinal) -lt 0 -or
    $environment.IndexOf('INFERENCEOS_TARGET=x86_64-elf', [StringComparison]::Ordinal) -lt 0) {
    throw 'Generated environment file is incomplete.'
}

[pscustomobject]@{
    Passed = $true
    InstallTreeDigest = $secondDigest
    ProfilesVerified = $profiles.Count
    PinnedComponentsVerified = $expected.Count
    ArtifactDirectory = $caseRoot
}
