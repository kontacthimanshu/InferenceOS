<#
.SYNOPSIS
Validates the static VFS, storage, and presentation dependency boundaries.

.DESCRIPTION
Scans every production C/header file below src/ and rejects forbidden includes or
direct function references. The enforced rules are deliberately conservative:

  DEP001/DEP002  GUI, Shell, and applications cannot call storage layers directly.
  DEP003/DEP004  Filesystems cannot depend on presentation or a storage driver.
  DEP005/DEP006  VFS cannot depend on a concrete filesystem or storage driver.
  DEP007/DEP008  Source outside block/ and the driver cannot call virtio-blk directly.

The optional JSON evidence contains only repository-relative paths and stable rule
metadata, so identical source trees produce identical reports.

.PARAMETER RepositoryRoot
Repository containing the src/ production tree.

.PARAMETER SelfTest
Runs positive and negative fixtures for every enforced rule before scanning source.

.PARAMETER EvidencePath
Writes a deterministic JSON report. A relative path is resolved from RepositoryRoot.

.EXAMPLE
./tools/test/validate_dependencies.ps1 -SelfTest `
  -EvidencePath build/validation/dependency-boundaries.json
#>
[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '../..'),
    [switch]$SelfTest,
    [string]$EvidencePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$rules = @(
    [pscustomobject]@{
        Scope = 'GUI, Shell, and applications'
        Boundary = 'Mediated display-safe services are the only storage-facing interface.'
        Path = '^src/(?:gui|shell|applications)/'
        IncludeRule = 'DEP001'
        Include = '^inferenceos/(?:vfs(?:[.]h|/.*)|fs/.*|block[.]h|drivers/virtio_blk[.]h)$'
        SymbolRule = 'DEP002'
        Symbol = '\b(?:vfs_|ios_fs_|block_|virtio_blk_)[A-Za-z0-9_]*\s*\('
        Message = 'GUI, Shell, and applications must use mediated display-safe services, not storage-layer APIs.'
    },
    [pscustomobject]@{
        Scope = 'InferenceOS-FS implementations'
        Boundary = 'Filesystem code stays below presentation and above the generic block layer.'
        Path = '^src/filesystems/'
        IncludeRule = 'DEP003'
        Include = '^inferenceos/(?:gui/|cui|shell|drivers/virtio_blk[.]h)'
        SymbolRule = 'DEP004'
        Symbol = '\b(?:ios_gui_|ios_cui_|ios_shell_|virtio_blk_)[A-Za-z0-9_]*\s*\('
        Message = 'Filesystem implementations must remain below presentation and storage-driver layers.'
    },
    [pscustomobject]@{
        Scope = 'VFS'
        Boundary = 'VFS stays filesystem-independent and storage-driver-independent.'
        Path = '^src/vfs/'
        IncludeRule = 'DEP005'
        Include = '^inferenceos/(?:fs/.*|drivers/virtio_blk[.]h)$'
        SymbolRule = 'DEP006'
        Symbol = '\b(?:ios_fs_|virtio_blk_)[A-Za-z0-9_]*\s*\('
        Message = 'VFS must not depend on InferenceOS-FS internals or a concrete storage driver.'
    },
    [pscustomobject]@{
        Scope = 'All source outside the block layer and virtio-blk driver'
        Boundary = 'Only the generic block layer communicates with the storage driver.'
        Path = '^src/(?!block/|drivers/virtio_blk/)'
        IncludeRule = 'DEP007'
        Include = '^inferenceos/drivers/virtio_blk[.]h$'
        SymbolRule = 'DEP008'
        Symbol = '\bvirtio_blk_[A-Za-z0-9_]*\s*\('
        Message = 'Only the generic block layer may communicate with the virtio-blk driver.'
    }
)

function Get-LineNumber {
    param([string]$Text, [int]$Offset)
    if ($Offset -le 0) { return 1 }
    return ([regex]::Matches($Text.Substring(0, $Offset), "`n").Count + 1)
}

function Find-DependencyViolations {
    param(
        [string]$RelativePath,
        [string]$Content
    )

    $normalizedPath = $RelativePath.Replace('\', '/')
    $found = [Collections.Generic.List[object]]::new()
    foreach ($rule in $rules) {
        if ($normalizedPath -notmatch $rule.Path) { continue }
        foreach ($match in [regex]::Matches(
            $Content, '(?m)^\s*#\s*include\s*[<"](?<header>[^>"]+)[>"]')) {
            if ($match.Groups['header'].Value -match $rule.Include) {
                $found.Add([pscustomobject]@{
                    Path = $normalizedPath
                    Line = Get-LineNumber $Content $match.Index
                    Rule = $rule.IncludeRule
                    Message = $rule.Message
                })
            }
        }
        foreach ($match in [regex]::Matches($Content, $rule.Symbol)) {
            $found.Add([pscustomobject]@{
                Path = $normalizedPath
                Line = Get-LineNumber $Content $match.Index
                Rule = $rule.SymbolRule
                Message = $rule.Message
            })
        }
    }
    return @($found)
}

function Invoke-SelfTest {
    $fixtures = @(
        @{ Name = 'safe GUI DTO'; Path = 'src/gui/file_explorer/model.c'; Text = '#include <inferenceos/display_safe_entry.h>'; Count = 0 },
        @{ Name = 'mediating kernel VFS'; Path = 'src/kernel/syscall/file_view.c'; Text = '#include <inferenceos/vfs.h>'; Count = 0 },
        @{ Name = 'filesystem generic block include'; Path = 'src/filesystems/inferenceos_fs/file.c'; Text = '#include <inferenceos/block.h>'; Count = 0 },
        @{ Name = 'GUI raw filesystem include'; Path = 'src/gui/file_explorer/model.c'; Text = '#include <inferenceos/fs/directory.h>'; Count = 1; Rule = 'DEP001' },
        @{ Name = 'Shell direct VFS call'; Path = 'src/shell/service.c'; Text = 'return vfs_list(path);'; Count = 1; Rule = 'DEP002' },
        @{ Name = 'filesystem GUI include'; Path = 'src/filesystems/inferenceos_fs/file.c'; Text = '#include <inferenceos/gui/window.h>'; Count = 1; Rule = 'DEP003' },
        @{ Name = 'filesystem driver call'; Path = 'src/filesystems/inferenceos_fs/file.c'; Text = 'return ios_gui_refresh();'; Count = 1; Rule = 'DEP004' },
        @{ Name = 'VFS filesystem include'; Path = 'src/vfs/mount.c'; Text = '#include <inferenceos/fs/mount.h>'; Count = 1; Rule = 'DEP005' },
        @{ Name = 'VFS filesystem call'; Path = 'src/vfs/mount.c'; Text = 'return ios_fs_mount_root();'; Count = 1; Rule = 'DEP006' },
        @{ Name = 'kernel storage-driver include'; Path = 'src/kernel/power.c'; Text = '#include <inferenceos/drivers/virtio_blk.h>'; Count = 1; Rule = 'DEP007' },
        @{ Name = 'CUI storage-driver call'; Path = 'src/cui/fs_commands.c'; Text = 'return virtio_blk_poll(device);'; Count = 1; Rule = 'DEP008' }
    )
    foreach ($fixture in $fixtures) {
        $result = @(Find-DependencyViolations $fixture.Path $fixture.Text)
        if ($result.Count -ne $fixture.Count) {
            throw "Dependency-validator fixture '$($fixture.Name)' expected $($fixture.Count) violation(s), found $($result.Count)."
        }
        if ($fixture.Count -ne 0 -and $result[0].Rule -ne $fixture.Rule) {
            throw "Dependency-validator fixture '$($fixture.Name)' expected rule $($fixture.Rule), found $($result[0].Rule)."
        }
    }
    Write-Host "Dependency validator self-test passed ($($fixtures.Count) fixtures)."
    return $fixtures.Count
}

$selfTestFixtureCount = 0
if ($SelfTest) { $selfTestFixtureCount = Invoke-SelfTest }

$root = [IO.Path]::GetFullPath($RepositoryRoot)
$sourceRoot = Join-Path $root 'src'
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "Source root does not exist: $sourceRoot"
}

$violations = [Collections.Generic.List[object]]::new()
$rootPrefix = $root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$rootPrefix += [IO.Path]::DirectorySeparatorChar
$files = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object {
    $_.Extension -in @('.c', '.h')
})
foreach ($file in $files) {
    if (-not $file.FullName.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Source file escaped repository root: $($file.FullName)"
    }
    $relative = $file.FullName.Substring($rootPrefix.Length).Replace('\', '/')
    foreach ($violation in @(Find-DependencyViolations $relative ([IO.File]::ReadAllText($file.FullName)))) {
        $violations.Add($violation)
    }
}

function Write-DependencyEvidence {
    param(
        [string]$Path,
        [object[]]$SortedViolations
    )

    if ([string]::IsNullOrWhiteSpace($Path)) { return }
    $resolvedPath = if ([IO.Path]::IsPathRooted($Path)) {
        [IO.Path]::GetFullPath($Path)
    } else {
        [IO.Path]::GetFullPath((Join-Path $root $Path))
    }
    $parent = Split-Path -Parent $resolvedPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        [IO.Directory]::CreateDirectory($parent) | Out-Null
    }
    $documentedRules = @($rules | ForEach-Object {
        [ordered]@{
            includeRule = $_.IncludeRule
            symbolRule = $_.SymbolRule
            scope = $_.Scope
            boundary = $_.Boundary
            pathPattern = $_.Path
            forbiddenIncludePattern = $_.Include
            forbiddenSymbolPattern = $_.Symbol
        }
    })
    $report = [ordered]@{
        schemaVersion = 1
        validator = 'inferenceos-dependency-boundaries'
        status = if ($SortedViolations.Count -eq 0) { 'passed' } else { 'failed' }
        sourceRoot = 'src'
        inspectedFileCount = $files.Count
        selfTest = [ordered]@{
            requested = [bool]$SelfTest
            fixtureCount = $selfTestFixtureCount
            passed = [bool]$SelfTest
        }
        rules = $documentedRules
        violations = @($SortedViolations | ForEach-Object {
            [ordered]@{
                path = $_.Path
                line = $_.Line
                rule = $_.Rule
                message = $_.Message
            }
        })
    }
    $json = $report | ConvertTo-Json -Depth 8
    [IO.File]::WriteAllText(
        $resolvedPath,
        $json + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false)
    )
    Write-Host "Dependency validation evidence: $resolvedPath"
}

$sortedViolations = @($violations | Sort-Object Path, Line, Rule)
Write-DependencyEvidence -Path $EvidencePath -SortedViolations $sortedViolations

if ($violations.Count -ne 0) {
    foreach ($violation in $sortedViolations) {
        Write-Output ("{0}:{1}: {2}: {3}" -f $violation.Path, $violation.Line,
            $violation.Rule, $violation.Message)
    }
    Write-Output "Dependency validation failed with $($violations.Count) violation(s)."
    exit 1
}

Write-Host "Dependency validation passed ($($files.Count) production C/header files inspected)."
exit 0
