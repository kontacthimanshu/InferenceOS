[CmdletBinding()]
param(
    [string]$Distribution = "Ubuntu",
    [string]$InstallRoot,
    [string]$DownloadRoot,
    [string]$EnvironmentFile,
    [switch]$Check
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Convert-ToWslPath {
    param([Parameter(Mandatory)][string]$Path)

    $resolved = [System.IO.Path]::GetFullPath($Path)
    $converted = & wsl.exe --distribution $Distribution -- wslpath -a -- $resolved
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($converted)) {
        throw "Unable to convert '$resolved' to a WSL path for distribution '$Distribution'."
    }
    return $converted.Trim()
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw "WSL is required. Install WSL and an Ubuntu distribution before running this bootstrap."
}

$distributionNames = @(& wsl.exe --list --quiet) | ForEach-Object { $_.Trim("`0", " ") } | Where-Object { $_ }
if ($LASTEXITCODE -ne 0 -or $Distribution -notin $distributionNames) {
    throw "WSL distribution '$Distribution' was not found. Available distributions: $($distributionNames -join ', ')"
}

$scriptRoot = Split-Path -Parent $PSCommandPath
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot "..\.."))
$wslScript = Convert-ToWslPath (Join-Path $scriptRoot "wsl-ubuntu.sh")

$arguments = @($wslScript)
if ($InstallRoot) {
    $arguments += @("--install-root", (Convert-ToWslPath $InstallRoot))
}
if ($DownloadRoot) {
    $arguments += @("--download-root", (Convert-ToWslPath $DownloadRoot))
}
if ($EnvironmentFile) {
    $arguments += @("--env-file", (Convert-ToWslPath $EnvironmentFile))
}
if ($Check) {
    $arguments += "--check"
}

Write-Host "Bootstrapping InferenceOS in WSL distribution '$Distribution'."
Write-Host "Repository: $repositoryRoot"
& wsl.exe --distribution $Distribution -- bash @arguments
if ($LASTEXITCODE -ne 0) {
    throw "InferenceOS bootstrap failed with exit code $LASTEXITCODE."
}
