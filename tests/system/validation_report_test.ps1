[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '../..'),
    [string]$ArtifactDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

$root = [IO.Path]::GetFullPath($RepositoryRoot)
$runner = Join-Path $root 'tools/test/generate_validation_report.ps1'
if (-not [IO.File]::Exists($runner)) { throw 'Validation report generator is missing.' }
$caseRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $root 'build/system/validation-report-contract'
} else { [IO.Path]::GetFullPath($ArtifactDirectory) }
$evidence = Join-Path $caseRoot 'evidence'
$outputA = Join-Path $caseRoot 'report-a'
$outputB = Join-Path $caseRoot 'report-b'
foreach ($directory in @($evidence, $outputA, $outputB)) {
    [IO.Directory]::CreateDirectory($directory) | Out-Null
}

$testCases = @(
    '<testcase name="fs-metadata-unit"/>',
    '<testcase name="directory-record-unit"/>',
    '<testcase name="registry-benchmark-contract"/>',
    '<testcase name="registry-benchmark-report-system"/>',
    '<testcase name="proprietary-routing-contract"><failure message="injected"/></testcase>',
    '<testcase name="proprietary-viewer-integration"/>'
) -join ''
$junit = "<testsuites><testsuite name=`"synthetic`">$testCases</testsuite></testsuites>`n"
Write-Utf8NoBom (Join-Path $evidence 'gcc-results.xml') $junit
$clangJunit = $junit.Replace(
    '<testcase name="proprietary-routing-contract"><failure message="injected"/></testcase>',
    '<testcase name="proprietary-routing-contract"/>'
)
Write-Utf8NoBom (Join-Path $evidence 'clang-results.xml') $clangJunit
$benchmark = [ordered]@{
    schema_version = 1
    measurement_source = 'qemu-tcg-markers'
    phases = @([ordered]@{ name = 'query-cold'; instructions = 100; conditional_branches = 10; latency_ns = 20 })
    gate = [ordered]@{ status = 'default-off'; default_enabled = $false }
}
Write-Utf8NoBom (Join-Path $evidence 'registry-benchmark-report.json') `
    (($benchmark | ConvertTo-Json -Depth 6) + "`n")

& $runner -RepositoryRoot $root -EvidenceDirectory $evidence -OutputDirectory $outputA
if ($LASTEXITCODE -ne 0) { throw 'Validation report generation failed.' }
& $runner -RepositoryRoot $root -EvidenceDirectory $evidence -OutputDirectory $outputB
if ($LASTEXITCODE -ne 0) { throw 'Repeated validation report generation failed.' }

$jsonA = Join-Path $outputA 'validation-traceability.json'
$jsonB = Join-Path $outputB 'validation-traceability.json'
$markdownA = Join-Path $outputA 'validation-traceability.md'
if (-not [IO.File]::Exists($jsonA) -or -not [IO.File]::Exists($markdownA)) {
    throw 'Validation report outputs are missing.'
}
if ((Get-FileHash -LiteralPath $jsonA -Algorithm SHA256).Hash -cne
    (Get-FileHash -LiteralPath $jsonB -Algorithm SHA256).Hash) {
    throw 'Validation traceability JSON is not deterministic.'
}
$report = [IO.File]::ReadAllText($jsonA) | ConvertFrom-Json
if ($report.schemaVersion -ne 1 -or $report.criteriaRange -cne 'SC-001-SC-020' -or
    @($report.criteria).Count -ne 20 -or @($report.criteria.id | Sort-Object -Unique).Count -ne 20) {
    throw 'Validation report does not contain the complete SC-001-SC-020 contract.'
}
$sc006 = @($report.criteria | Where-Object { $_.id -ceq 'SC-006' })[0]
$sc013 = @($report.criteria | Where-Object { $_.id -ceq 'SC-013' })[0]
$sc017 = @($report.criteria | Where-Object { $_.id -ceq 'SC-017' })[0]
if ($sc006.status -cne 'passed') { throw 'Passing dual-compiler evidence was not aggregated.' }
if ($sc013.status -cne 'failed') { throw 'A failing CTest result was not propagated.' }
if ($sc017.status -cne 'passed') { throw 'Registry benchmark report evidence was not recognized.' }
if (-not [IO.File]::ReadAllText($markdownA).Contains('SC-020')) {
    throw 'Markdown traceability omits SC-020.'
}

& $runner -RepositoryRoot $root -EvidenceDirectory $evidence -OutputDirectory $outputB -RequireComplete
$requireCompleteExitCode = $LASTEXITCODE
if ($requireCompleteExitCode -eq 0) {
    throw 'RequireComplete accepted incomplete success-criterion evidence.'
}
$global:LASTEXITCODE = 0

[pscustomobject]@{ Passed = $true; Criteria = @($report.criteria).Count; JsonPath = $jsonA }
