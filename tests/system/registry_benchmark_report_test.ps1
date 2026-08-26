[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..\..'),
    [string]$ArtifactDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Utf8NoBom([string]$Path, [string[]]$Lines) {
    [System.IO.File]::WriteAllLines(
        $Path, $Lines, [System.Text.UTF8Encoding]::new($false)
    )
}

$root = [System.IO.Path]::GetFullPath($RepositoryRoot)
$runner = Join-Path $root 'tools/test/run_registry_benchmark.ps1'
if (-not [System.IO.File]::Exists($runner)) {
    throw "Registry benchmark runner '$runner' is missing."
}
$artifactRoot = if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    Join-Path $root 'build/system/registry-benchmark-report'
} else {
    [System.IO.Path]::GetFullPath($ArtifactDirectory)
}
$caseRoot = Join-Path $artifactRoot "run-$PID"
[System.IO.Directory]::CreateDirectory($caseRoot) | Out-Null
$tracePath = Join-Path $caseRoot 'tcg-markers.log'
$reportDirectory = Join-Path $caseRoot 'report'
$lines = [System.Collections.Generic.List[string]]::new()
$phases = @('cold-query', 'warm-query', 'durable-save')

for ($sample = 0; $sample -lt 5; ++$sample) {
    foreach ($mode in @('disabled', 'enabled')) {
        $lines.Add(
            "INFERENCEOS:REGISTRY_BENCH_CORPUS mode=$mode seed=1a2b3c4d corpus=09964dc0 queries=3efcdd8b"
        )
        foreach ($phase in $phases) {
            $disabledBase = switch ($phase) {
                'cold-query' { 1000 }
                'warm-query' { 800 }
                default { 500 }
            }
            $enabledBase = switch ($phase) {
                'cold-query' { 840 }
                'warm-query' { 680 }
                default { 520 }
            }
            $instructions = $(if ($mode -eq 'enabled') { $enabledBase } else { $disabledBase }) + $sample
            $branches = [int][Math]::Floor($instructions / 4)
            $latency = $instructions * 10
            $lines.Add("INFERENCEOS:REGISTRY_BENCH_BEGIN mode=$mode phase=$phase")
            $lines.Add(
                "INFERENCEOS:REGISTRY_BENCH_COUNTER mode=$mode phase=$phase sample=$sample instructions=$instructions conditional-branches=$branches latency-ns=$latency"
            )
            $lines.Add("INFERENCEOS:REGISTRY_BENCH_END mode=$mode phase=$phase")
        }
        $lines.Add(
            "INFERENCEOS:REGISTRY_BENCH_RESULT mode=$mode correctness=9eab6129 cold-results=24 warm-results=24 durable-saves=1"
        )
    }
}
Write-Utf8NoBom $tracePath $lines

& $runner -TracePath $tracePath -OutputDirectory $reportDirectory `
    -BuildVersion 'test-build' -CompilerVersion 'GNU 16.2.0' `
    -QemuVersion '11.1.0' `
    -ImageChecksum ('a' * 64) | Out-Null

$jsonPath = Join-Path $reportDirectory 'registry-benchmark-report.json'
$markdownPath = Join-Path $reportDirectory 'registry-benchmark-report.md'
if (-not [System.IO.File]::Exists($jsonPath) -or
    -not [System.IO.File]::Exists($markdownPath)) {
    throw 'Registry benchmark reports were not generated.'
}
$report = Get-Content -Raw -LiteralPath $jsonPath | ConvertFrom-Json
if ($report.schema_version -ne 1 -or
    $report.measurement_source -cne 'qemu-tcg-markers' -or
    $report.sample_count -ne 5 -or
    -not $report.correctness.matched -or
    $report.gate.status -cne 'proposal-eligible' -or
    $report.gate.default_enabled -or
    $report.hardware_cycles.status -cne 'not-collected' -or
    $report.inputs.seed -cne '1a2b3c4d' -or
    $report.inputs.image_sha256 -cne ('a' * 64)) {
    throw 'Registry benchmark JSON report does not satisfy the matched TCG contract.'
}
$coldEnabled = @($report.phases | Where-Object {
    $_.mode -ceq 'enabled' -and $_.phase -ceq 'cold-query'
})
if ($coldEnabled.Count -ne 1 -or
    $coldEnabled[0].instructions.median -ge 900 -or
    $coldEnabled[0].instructions.p95 -lt $coldEnabled[0].instructions.median) {
    throw 'Registry benchmark statistics are incomplete or incorrectly ordered.'
}
$markdown = Get-Content -Raw -LiteralPath $markdownPath
if (-not $markdown.Contains('Default remains disabled') -or
    -not $markdown.Contains('Hardware cycles were not collected')) {
    throw 'Registry benchmark Markdown report omits required qualification.'
}

$hardwarePath = Join-Path $caseRoot 'pinned-host-cycles.json'
$hardwareSamples = [System.Collections.Generic.List[object]]::new()
for ($sample = 0; $sample -lt 5; ++$sample) {
    foreach ($mode in @('disabled', 'enabled')) {
        foreach ($phase in $phases) {
            $hardwareSamples.Add([ordered]@{
                mode = $mode
                phase = $phase
                sample = $sample
                cycles = 5000 + $sample
            })
        }
    }
}
$hardware = [ordered]@{
    schema_version = 1
    profile = [ordered]@{
        pinned_host = $true
        description = 'test-host/cpu0/perf'
    }
    samples = @($hardwareSamples)
}
[System.IO.File]::WriteAllText(
    $hardwarePath, ($hardware | ConvertTo-Json -Depth 6) + "`n",
    [System.Text.UTF8Encoding]::new($false)
)
$hardwareReportDirectory = Join-Path $caseRoot 'hardware-report'
& $runner -TracePath $tracePath -OutputDirectory $hardwareReportDirectory `
    -BuildVersion 'test-build' -CompilerVersion 'GNU 16.2.0' `
    -QemuVersion '11.1.0' -ImageChecksum ('c' * 64) `
    -HardwareCounterPath $hardwarePath | Out-Null
$hardwareReport = Get-Content -Raw -LiteralPath (
    Join-Path $hardwareReportDirectory 'registry-benchmark-report.json'
) | ConvertFrom-Json
if ($hardwareReport.hardware_cycles.status -cne 'qualified-pinned-host' -or
    $hardwareReport.hardware_cycles.phases.Count -ne 6) {
    throw 'Separately qualified pinned-host cycle measurements were not reported.'
}

$badTrace = Join-Path $caseRoot 'mismatched-correctness.log'
$badLines = @($lines)
for ($index = 0; $index -lt $badLines.Count; ++$index) {
    if ($badLines[$index].Contains('mode=enabled correctness=9eab6129')) {
        $badLines[$index] = $badLines[$index].Replace(
            'correctness=9eab6129', 'correctness=deadbeef'
        )
    }
}
Write-Utf8NoBom $badTrace $badLines
$mismatchRejected = $false
try {
    & $runner -TracePath $badTrace -OutputDirectory (Join-Path $caseRoot 'bad-report') `
        -BuildVersion 'test-build' -CompilerVersion 'GNU 16.2.0' `
        -QemuVersion '11.1.0' -ImageChecksum ('b' * 64) | Out-Null
} catch {
    $mismatchRejected = $_.Exception.Message.Contains('correctness')
}
if (-not $mismatchRejected) {
    throw 'Registry benchmark report accepted mismatched enabled/disabled correctness.'
}

[pscustomobject]@{
    Passed = $true
    Report = $jsonPath
    ArtifactDirectory = $caseRoot
}
