[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '../..'),
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$rules = @(
    [pscustomobject]@{
        Path = '^src/(?:gui|shell|applications)/'
        IncludeRule = 'DEP001'
        Include = '^inferenceos/(?:vfs(?:[.]h|/.*)|fs/.*|block[.]h|drivers/virtio_blk[.]h)$'
        SymbolRule = 'DEP002'
        Symbol = '\b(?:vfs_|ios_fs_|block_|virtio_blk_)[A-Za-z0-9_]*\s*\('
        Message = 'GUI, Shell, and applications must use mediated display-safe services, not storage-layer APIs.'
    },
    [pscustomobject]@{
        Path = '^src/filesystems/'
        IncludeRule = 'DEP003'
        Include = '^inferenceos/(?:gui/|cui|shell|drivers/virtio_blk[.]h)'
        SymbolRule = 'DEP004'
        Symbol = '\b(?:ios_gui_|ios_cui_|ios_shell_|virtio_blk_)[A-Za-z0-9_]*\s*\('
        Message = 'Filesystem implementations must remain below presentation and storage-driver layers.'
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
        @{ Name = 'GUI raw filesystem include'; Path = 'src/gui/file_explorer/model.c'; Text = '#include <inferenceos/fs/directory.h>'; Count = 1; Rule = 'DEP001' },
        @{ Name = 'Shell direct VFS call'; Path = 'src/shell/service.c'; Text = 'return vfs_list(path);'; Count = 1; Rule = 'DEP002' },
        @{ Name = 'filesystem GUI include'; Path = 'src/filesystems/inferenceos_fs/file.c'; Text = '#include <inferenceos/gui/window.h>'; Count = 1; Rule = 'DEP003' }
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
}

if ($SelfTest) { Invoke-SelfTest }

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

if ($violations.Count -ne 0) {
    foreach ($violation in $violations | Sort-Object Path, Line, Rule) {
        Write-Output ("{0}:{1}: {2}: {3}" -f $violation.Path, $violation.Line,
            $violation.Rule, $violation.Message)
    }
    Write-Output "Dependency validation failed with $($violations.Count) violation(s)."
    exit 1
}

Write-Host "Dependency validation passed ($($files.Count) production C/header files inspected)."
exit 0
