[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..\..')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath($RepositoryRoot)
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    throw "Repository root does not exist: $root"
}

$violations = [Collections.Generic.List[object]]::new()

function Add-Violation {
    param(
        [string]$Path,
        [int]$Line,
        [string]$Rule,
        [string]$Message
    )

    $violations.Add([pscustomobject]@{
        Path = $Path
        Line = $Line
        Rule = $Rule
        Message = $Message
    })
}

function Get-LineNumber {
    param([string]$Text, [int]$Offset)

    if ($Offset -le 0) {
        return 1
    }

    return ([regex]::Matches($Text.Substring(0, $Offset), "`n").Count + 1)
}

function Test-ForbiddenPattern {
    param(
        [string]$RelativePath,
        [string]$Content,
        [string]$Rule,
        [string]$Pattern,
        [string]$Message
    )

    foreach ($match in [regex]::Matches($Content, $Pattern, [Text.RegularExpressions.RegexOptions]::Multiline)) {
        Add-Violation $RelativePath (Get-LineNumber $Content $match.Index) $Rule $Message
    }
}

$gitFiles = @(& git -C $root ls-files --cached --others --exclude-standard 2>$null)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to enumerate repository files with git at: $root"
}

$files = @(
    $gitFiles |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_.Replace('\', '/') } |
        Sort-Object -Unique
)

$sourceExtensions = @('.c', '.h', '.S', '.s', '.cc', '.cpp', '.cxx', '.hh', '.hpp', '.hxx', '.asm')
$approvedProductionExtensions = @('.c', '.h', '.S')
$compilerHeader = 'src/kernel/include/inferenceos/compiler.h'
$approvedAttributes = @('aligned', 'packed', 'section', 'used', 'ms_abi', 'sysv_abi')

foreach ($relativePath in $files) {
    $extension = [IO.Path]::GetExtension($relativePath)
    if ($sourceExtensions -cnotcontains $extension) {
        continue
    }

    $isProduction = $relativePath.StartsWith('src/', [StringComparison]::OrdinalIgnoreCase)
    $isTest = $relativePath.StartsWith('tests/', [StringComparison]::OrdinalIgnoreCase)

    if (-not $isProduction -and -not $isTest) {
        Add-Violation $relativePath 1 'LAYOUT001' 'C, header, and assembly files must be beneath src/ or tests/.'
        continue
    }

    if ($approvedProductionExtensions -cnotcontains $extension) {
        Add-Violation $relativePath 1 'LANG001' 'Only .c, .h, and preprocessed .S source formats are approved.'
        continue
    }

    if (-not $isProduction) {
        continue
    }

    $absolutePath = Join-Path $root ($relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))
    $content = [IO.File]::ReadAllText($absolutePath)
    $isCompilerHeader = $relativePath.Equals($compilerHeader, [StringComparison]::OrdinalIgnoreCase)
    $isArchitectureSource = $relativePath.StartsWith('src/arch/x86_64/', [StringComparison]::OrdinalIgnoreCase)

    if (-not $isCompilerHeader) {
        Test-ForbiddenPattern $relativePath $content 'EXT001' '\b__attribute__\s*\(' 'Direct __attribute__ spelling is owned by compiler.h.'
        Test-ForbiddenPattern $relativePath $content 'EXT002' '\b__(?:GNUC|clang)__\b' 'Compiler identification macros are owned by compiler.h.'
    }

    if (-not $isArchitectureSource) {
        Test-ForbiddenPattern $relativePath $content 'EXT003' '\b(?:__asm__|__asm|asm)\s*(?:volatile\s*)?\(' 'Inline assembly is restricted to src/arch/x86_64/.'
    }

    Test-ForbiddenPattern $relativePath $content 'EXT004' '\b(?:typeof|__typeof__?)\s*\(' 'GNU typeof is not allowlisted.'
    Test-ForbiddenPattern $relativePath $content 'EXT005' '\(\s*\{' 'GNU statement expressions are not allowlisted.'
    Test-ForbiddenPattern $relativePath $content 'EXT006' '\[[ \t]*0[ \t]*\](?=[ \t]*(?:[,;)]|$))' 'Zero-length arrays are not allowlisted.'
    Test-ForbiddenPattern $relativePath $content 'EXT007' '\bcase\s+[^:\r\n]+\.\.\.[^:\r\n]+:' 'GNU case ranges are not allowlisted.'
    Test-ForbiddenPattern $relativePath $content 'EXT008' '\bgoto\s+\*|&&[A-Za-z_][A-Za-z0-9_]*' 'Computed goto is not allowlisted.'
    Test-ForbiddenPattern $relativePath $content 'EXT009' '\b__(?:atomic|sync)_[A-Za-z0-9_]+' 'Compiler-specific atomic builtins are not allowlisted.'
    Test-ForbiddenPattern $relativePath $content 'EXT010' '\b__builtin_[A-Za-z0-9_]+' 'Compiler builtins are not allowlisted.'
    Test-ForbiddenPattern $relativePath $content 'EXT011' '^\s*#\s*pragma\s+(?:GCC|clang)\b' 'Compiler-specific pragmas are not allowlisted.'
    Test-ForbiddenPattern $relativePath $content 'EXT012' '\b__extension__\b' 'GNU diagnostic suppression is not allowlisted.'

    if ($isCompilerHeader) {
        foreach ($match in [regex]::Matches($content, '\b__attribute__\s*\(\(\s*(?<name>[A-Za-z_][A-Za-z0-9_]*)')) {
            $name = $match.Groups['name'].Value
            if ($approvedAttributes -cnotcontains $name) {
                Add-Violation $relativePath (Get-LineNumber $content $match.Index) 'EXT013' "Attribute '$name' is not allowlisted."
            }
        }

        foreach ($match in [regex]::Matches($content, '\b__attribute__\s*\(\((?<body>.*?)\)\)', [Text.RegularExpressions.RegexOptions]::Singleline)) {
            if ($match.Groups['body'].Value.Contains(',')) {
                Add-Violation $relativePath (Get-LineNumber $content $match.Index) 'EXT014' 'Use one approved attribute per portability macro.'
            }
        }
    }
}

if ($violations.Count -gt 0) {
    foreach ($violation in $violations | Sort-Object Path, Line, Rule) {
        Write-Output ("{0}:{1}: {2}: {3}" -f $violation.Path, $violation.Line, $violation.Rule, $violation.Message)
    }

    Write-Output ("Source layout validation failed with {0} violation(s)." -f $violations.Count)
    exit 1
}

Write-Host ("Source layout validation passed ({0} repository file(s) inspected)." -f $files.Count)
exit 0
