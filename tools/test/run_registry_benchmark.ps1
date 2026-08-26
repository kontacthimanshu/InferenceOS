[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$TracePath,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$BuildVersion,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$CompilerVersion,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$QemuVersion,
    [string]$ImagePath,
    [string]$ImageChecksum,
    [string]$HardwareCounterPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$phaseOrder = @('cold-query', 'warm-query', 'durable-save')
$modeOrder = @('disabled', 'enabled')
$corpusPattern = [regex]::new(
    '^INFERENCEOS:REGISTRY_BENCH_CORPUS mode=(disabled|enabled) seed=([0-9A-Fa-f]{8}) corpus=([0-9A-Fa-f]{8}) queries=([0-9A-Fa-f]{8})$'
)
$beginPattern = [regex]::new(
    '^INFERENCEOS:REGISTRY_BENCH_BEGIN mode=(disabled|enabled) phase=(cold-query|warm-query|durable-save)$'
)
$counterPattern = [regex]::new(
    '^INFERENCEOS:REGISTRY_BENCH_COUNTER mode=(disabled|enabled) phase=(cold-query|warm-query|durable-save) sample=([0-9]+) instructions=([0-9]+) conditional-branches=([0-9]+) latency-ns=([0-9]+)$'
)
$endPattern = [regex]::new(
    '^INFERENCEOS:REGISTRY_BENCH_END mode=(disabled|enabled) phase=(cold-query|warm-query|durable-save)$'
)
$resultPattern = [regex]::new(
    '^INFERENCEOS:REGISTRY_BENCH_RESULT mode=(disabled|enabled) correctness=([0-9A-Fa-f]{8}) cold-results=([0-9]+) warm-results=([0-9]+) durable-saves=([0-9]+)$'
)

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText(
        $Path, $Text, [System.Text.UTF8Encoding]::new($false)
    )
}

function Resolve-InputFile([string]$Path, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path)) { throw "$Description path is required." }
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not [System.IO.File]::Exists($resolved)) {
        throw "$Description '$resolved' does not exist."
    }
    return $resolved
}

function Assert-SameValue(
    [hashtable]$Values,
    [string]$Key,
    [object]$Value,
    [string]$Description
) {
    if ($Values.ContainsKey($Key)) {
        if ([string]$Values[$Key] -cne [string]$Value) {
            throw "Matched benchmark $Description differs for '$Key'."
        }
    } else {
        $Values[$Key] = $Value
    }
}

function Get-Statistics([uint64[]]$Values) {
    if ($null -eq $Values -or $Values.Count -eq 0) {
        throw 'Cannot summarize an empty measurement set.'
    }
    [uint64[]]$sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    $median = if (($sorted.Count % 2) -eq 1) {
        [double]$sorted[$middle]
    } else {
        ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
    }
    $p95Index = [Math]::Max(0, [int][Math]::Ceiling($sorted.Count * 0.95) - 1)
    return [ordered]@{
        count = $sorted.Count
        median = $median
        p95 = [uint64]$sorted[$p95Index]
        minimum = [uint64]$sorted[0]
        maximum = [uint64]$sorted[$sorted.Count - 1]
        spread = [uint64]($sorted[$sorted.Count - 1] - $sorted[0])
    }
}

function Get-ImprovementPercent([double]$Baseline, [double]$Candidate) {
    if ($Baseline -le 0) { throw 'A benchmark baseline median must be positive.' }
    return [Math]::Round((($Baseline - $Candidate) / $Baseline) * 100.0, 4)
}

function Get-RegressionPercent([double]$Baseline, [double]$Candidate) {
    if ($Baseline -le 0) { throw 'A benchmark baseline median must be positive.' }
    return [Math]::Round((($Candidate - $Baseline) / $Baseline) * 100.0, 4)
}

$resolvedTraces = @($TracePath | ForEach-Object {
    Resolve-InputFile $_ 'TCG marker trace'
})
if ($resolvedTraces.Count -eq 0) { throw 'At least one TCG marker trace is required.' }

$resolvedImage = $null
if (-not [string]::IsNullOrWhiteSpace($ImagePath)) {
    $resolvedImage = Resolve-InputFile $ImagePath 'Benchmark image'
    $computedImageChecksum = (Get-FileHash -LiteralPath $resolvedImage -Algorithm SHA256).Hash.ToLowerInvariant()
    if (-not [string]::IsNullOrWhiteSpace($ImageChecksum) -and
        $computedImageChecksum -cne $ImageChecksum.ToLowerInvariant()) {
        throw 'The supplied image checksum does not match the benchmark image.'
    }
    $ImageChecksum = $computedImageChecksum
}
if ([string]::IsNullOrWhiteSpace($ImageChecksum) -or
    $ImageChecksum -notmatch '^[0-9A-Fa-f]{64}$') {
    throw 'ImageChecksum must be a 64-character SHA-256 value when ImagePath is absent.'
}
$ImageChecksum = $ImageChecksum.ToLowerInvariant()

$measurements = [System.Collections.Generic.List[object]]::new()
$measurementKeys = @{}
$corpusValues = @{}
$resultValues = @{}
$resultCounts = @{ disabled = 0; enabled = 0 }
$activeWindow = $null
$activeCounter = $null

foreach ($trace in $resolvedTraces) {
    foreach ($lineValue in Get-Content -LiteralPath $trace) {
        $line = $lineValue.Trim()
        if ($line.Length -eq 0) { continue }
        $match = $corpusPattern.Match($line)
        if ($match.Success) {
            $mode = $match.Groups[1].Value
            Assert-SameValue $corpusValues 'seed' $match.Groups[2].Value.ToLowerInvariant() 'seed'
            Assert-SameValue $corpusValues 'corpus' $match.Groups[3].Value.ToLowerInvariant() 'corpus checksum'
            Assert-SameValue $corpusValues 'queries' $match.Groups[4].Value.ToLowerInvariant() 'query checksum'
            Assert-SameValue $corpusValues "seen-$mode" $true 'corpus mode'
            continue
        }
        $match = $beginPattern.Match($line)
        if ($match.Success) {
            if ($null -ne $activeWindow) {
                throw 'Nested registry benchmark begin markers are not allowed.'
            }
            $activeWindow = [pscustomobject]@{
                Mode = $match.Groups[1].Value
                Phase = $match.Groups[2].Value
                Trace = $trace
            }
            $activeCounter = $null
            continue
        }
        $match = $counterPattern.Match($line)
        if ($match.Success) {
            if ($null -eq $activeWindow -or $null -ne $activeCounter -or
                $activeWindow.Mode -cne $match.Groups[1].Value -or
                $activeWindow.Phase -cne $match.Groups[2].Value) {
                throw 'A TCG counter record is not paired with its active benchmark markers.'
            }
            [uint64]$sample = $match.Groups[3].Value
            $key = "$($activeWindow.Mode)|$($activeWindow.Phase)|$sample"
            if ($measurementKeys.ContainsKey($key)) {
                throw "Duplicate TCG measurement '$key'."
            }
            $activeCounter = [pscustomobject][ordered]@{
                mode = $activeWindow.Mode
                phase = $activeWindow.Phase
                sample = $sample
                instructions = [uint64]$match.Groups[4].Value
                conditional_branches = [uint64]$match.Groups[5].Value
                latency_ns = [uint64]$match.Groups[6].Value
                trace = $trace
            }
            if ($activeCounter.instructions -eq 0 -or
                $activeCounter.latency_ns -eq 0) {
                throw "TCG measurement '$key' contains a zero required counter."
            }
            $measurementKeys[$key] = $true
            continue
        }
        $match = $endPattern.Match($line)
        if ($match.Success) {
            if ($null -eq $activeWindow -or $null -eq $activeCounter -or
                $activeWindow.Mode -cne $match.Groups[1].Value -or
                $activeWindow.Phase -cne $match.Groups[2].Value) {
                throw 'A registry benchmark end marker is missing its paired begin/counter record.'
            }
            $measurements.Add($activeCounter)
            $activeWindow = $null
            $activeCounter = $null
            continue
        }
        $match = $resultPattern.Match($line)
        if ($match.Success) {
            if ($null -ne $activeWindow) {
                throw 'A registry benchmark result appeared inside a measurement window.'
            }
            $mode = $match.Groups[1].Value
            Assert-SameValue $resultValues "correctness-$mode" $match.Groups[2].Value.ToLowerInvariant() 'correctness digest'
            Assert-SameValue $resultValues "cold-$mode" ([uint64]$match.Groups[3].Value) 'cold result count'
            Assert-SameValue $resultValues "warm-$mode" ([uint64]$match.Groups[4].Value) 'warm result count'
            Assert-SameValue $resultValues "save-$mode" ([uint64]$match.Groups[5].Value) 'durable-save count'
            ++$resultCounts[$mode]
            continue
        }
        if ($line.StartsWith('INFERENCEOS:REGISTRY_BENCH_', [StringComparison]::Ordinal)) {
            throw "Unrecognized registry benchmark marker '$line'."
        }
    }
}
if ($null -ne $activeWindow) { throw 'The TCG marker trace ends inside a benchmark window.' }
foreach ($mode in $modeOrder) {
    if (-not $corpusValues.ContainsKey("seen-$mode") -or
        -not $resultValues.ContainsKey("correctness-$mode")) {
        throw "The TCG marker trace omits the '$mode' corpus or result record."
    }
}
if ($resultValues['correctness-disabled'] -cne $resultValues['correctness-enabled'] -or
    [uint64]$resultValues['cold-disabled'] -ne [uint64]$resultValues['cold-enabled'] -or
    [uint64]$resultValues['warm-disabled'] -ne [uint64]$resultValues['warm-enabled'] -or
    [uint64]$resultValues['save-disabled'] -ne [uint64]$resultValues['save-enabled']) {
    throw 'Enabled and disabled correctness results do not match.'
}

$sampleIds = @($measurements | Select-Object -ExpandProperty sample -Unique | Sort-Object)
if ($sampleIds.Count -lt 5) {
    throw 'At least five matched TCG samples are required for median, p95, and spread reporting.'
}
for ($index = 0; $index -lt $sampleIds.Count; ++$index) {
    if ([uint64]$sampleIds[$index] -ne [uint64]$index) {
        throw 'TCG sample identifiers must be contiguous and begin at zero.'
    }
}
foreach ($mode in $modeOrder) {
    if ($resultCounts[$mode] -ne $sampleIds.Count) {
        throw "The '$mode' result-marker count does not match the sample count."
    }
    foreach ($phase in $phaseOrder) {
        $count = @($measurements | Where-Object {
            $_.mode -ceq $mode -and $_.phase -ceq $phase
        }).Count
        if ($count -ne $sampleIds.Count) {
            throw "The '$mode/$phase' TCG sample set is incomplete."
        }
    }
}

$phaseReports = [System.Collections.Generic.List[object]]::new()
foreach ($mode in $modeOrder) {
    foreach ($phase in $phaseOrder) {
        $selection = @($measurements | Where-Object {
            $_.mode -ceq $mode -and $_.phase -ceq $phase
        } | Sort-Object sample)
        $phaseReports.Add([pscustomobject][ordered]@{
            mode = $mode
            phase = $phase
            instructions = Get-Statistics ([uint64[]]@($selection.instructions))
            conditional_branches = Get-Statistics ([uint64[]]@($selection.conditional_branches))
            latency_ns = Get-Statistics ([uint64[]]@($selection.latency_ns))
        })
    }
}

function Get-PhaseReport([string]$Mode, [string]$Phase) {
    return $phaseReports | Where-Object {
        $_.mode -ceq $Mode -and $_.phase -ceq $Phase
    } | Select-Object -First 1
}

$queryDisabled = @($measurements | Where-Object { $_.mode -ceq 'disabled' -and $_.phase -ne 'durable-save' })
$queryEnabled = @($measurements | Where-Object { $_.mode -ceq 'enabled' -and $_.phase -ne 'durable-save' })
$queryInstructionImprovement = Get-ImprovementPercent `
    (Get-Statistics ([uint64[]]@($queryDisabled.instructions))).median `
    (Get-Statistics ([uint64[]]@($queryEnabled.instructions))).median
$queryLatencyImprovement = Get-ImprovementPercent `
    (Get-Statistics ([uint64[]]@($queryDisabled.latency_ns))).median `
    (Get-Statistics ([uint64[]]@($queryEnabled.latency_ns))).median
$queryBranchImprovement = Get-ImprovementPercent `
    (Get-Statistics ([uint64[]]@($queryDisabled.conditional_branches))).median `
    (Get-Statistics ([uint64[]]@($queryEnabled.conditional_branches))).median
$saveDisabled = Get-PhaseReport 'disabled' 'durable-save'
$saveEnabled = Get-PhaseReport 'enabled' 'durable-save'
$saveLatencyRegression = Get-RegressionPercent `
    $saveDisabled.latency_ns.median $saveEnabled.latency_ns.median
$referenceEnvironmentMatched =
    $QemuVersion -match '(^|[^0-9])11[.]1[.]0([^0-9]|$)' -and
    ($CompilerVersion -match '(?i)(gcc|gnu)[^0-9]*16[.]2[.]0' -or
     $CompilerVersion -match '(?i)clang[^0-9]*22[.]1[.]8')
$benefitPassed = $queryInstructionImprovement -ge 10.0 -or
    $queryLatencyImprovement -ge 10.0
$savePassed = $saveLatencyRegression -le 5.0
$gatePassed = $referenceEnvironmentMatched -and $benefitPassed -and $savePassed

$hardwareCycles = [ordered]@{ status = 'not-collected'; profile = $null; phases = @() }
if (-not [string]::IsNullOrWhiteSpace($HardwareCounterPath)) {
    $hardwarePath = Resolve-InputFile $HardwareCounterPath 'Pinned-host hardware counter report'
    $hardware = Get-Content -Raw -LiteralPath $hardwarePath | ConvertFrom-Json
    if ($hardware.schema_version -ne 1 -or -not $hardware.profile.pinned_host -or
        [string]::IsNullOrWhiteSpace([string]$hardware.profile.description)) {
        throw 'Hardware cycle data must identify a version-1 pinned-host profile.'
    }
    $cycleReports = [System.Collections.Generic.List[object]]::new()
    foreach ($mode in $modeOrder) {
        foreach ($phase in $phaseOrder) {
            $cycles = @($hardware.samples | Where-Object {
                $_.mode -ceq $mode -and $_.phase -ceq $phase
            } | Sort-Object sample)
            if ($cycles.Count -ne $sampleIds.Count -or
                @($cycles | Where-Object { [uint64]$_.cycles -eq 0 }).Count -ne 0) {
                throw "Pinned-host cycle samples for '$mode/$phase' are incomplete."
            }
            for ($index = 0; $index -lt $cycles.Count; ++$index) {
                if ([uint64]$cycles[$index].sample -ne [uint64]$sampleIds[$index]) {
                    throw "Pinned-host cycle sample identifiers for '$mode/$phase' do not match TCG."
                }
            }
            $cycleReports.Add([pscustomobject][ordered]@{
                mode = $mode
                phase = $phase
                cycles = Get-Statistics ([uint64[]]@($cycles.cycles))
            })
        }
    }
    $hardwareCycles = [ordered]@{
        status = 'qualified-pinned-host'
        profile = $hardware.profile
        phases = @($cycleReports)
    }
}

$traceEvidence = foreach ($trace in $resolvedTraces) {
    [ordered]@{
        path = $trace
        sha256 = (Get-FileHash -LiteralPath $trace -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$report = [ordered]@{
    schema_version = 1
    measurement_source = 'qemu-tcg-markers'
    environment = [ordered]@{
        build_version = $BuildVersion
        compiler_version = $CompilerVersion
        qemu_version = $QemuVersion
        machine = 'q35'
        accelerator = 'tcg'
        cpu_count = 1
    }
    inputs = [ordered]@{
        seed = [string]$corpusValues.seed
        corpus_checksum = [string]$corpusValues.corpus
        query_checksum = [string]$corpusValues.queries
        image_path = $resolvedImage
        image_sha256 = $ImageChecksum
        traces = @($traceEvidence)
    }
    sample_count = $sampleIds.Count
    correctness = [ordered]@{
        matched = $true
        digest = [string]$resultValues['correctness-disabled']
        cold_result_count = [uint64]$resultValues['cold-disabled']
        warm_result_count = [uint64]$resultValues['warm-disabled']
        durable_save_count = [uint64]$resultValues['save-disabled']
    }
    phases = @($phaseReports)
    hardware_cycles = $hardwareCycles
    gate = [ordered]@{
        status = if ($gatePassed) { 'proposal-eligible' } elseif (-not $referenceEnvironmentMatched) { 'inconclusive-environment' } else { 'default-off' }
        correctness_required = $true
        reference_environment_matched = $referenceEnvironmentMatched
        query_instruction_improvement_percent = $queryInstructionImprovement
        query_latency_improvement_percent = $queryLatencyImprovement
        query_conditional_branch_improvement_percent = $queryBranchImprovement
        required_query_improvement_percent = 10.0
        durable_save_latency_regression_percent = $saveLatencyRegression
        maximum_durable_save_regression_percent = 5.0
        default_enabled = $false
    }
}

$output = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($output) | Out-Null
$jsonPath = Join-Path $output 'registry-benchmark-report.json'
$markdownPath = Join-Path $output 'registry-benchmark-report.md'
Write-Utf8NoBom $jsonPath (($report | ConvertTo-Json -Depth 12) + "`n")

$markdown = [System.Collections.Generic.List[string]]::new()
$markdown.Add('# Extension Registry Benchmark Report')
$markdown.Add('')
$markdown.Add("- Measurement source: QEMU TCG marker counters")
$markdown.Add("- QEMU: $QemuVersion")
$markdown.Add("- Build: $BuildVersion")
$markdown.Add("- Compiler: $CompilerVersion")
$markdown.Add("- Corpus seed/checksum: $($corpusValues.seed) / $($corpusValues.corpus)")
$markdown.Add("- Query checksum: $($corpusValues.queries)")
$markdown.Add("- Image SHA-256: $ImageChecksum")
$markdown.Add("- Matched samples per mode/phase: $($sampleIds.Count)")
$markdown.Add('')
$markdown.Add('| Mode | Phase | Instructions median/p95/spread | Branches median/p95/spread | Latency ns median/p95/spread |')
$markdown.Add('|---|---|---:|---:|---:|')
foreach ($phase in $phaseReports) {
    $markdown.Add(
        "| $($phase.mode) | $($phase.phase) | $($phase.instructions.median) / $($phase.instructions.p95) / $($phase.instructions.spread) | $($phase.conditional_branches.median) / $($phase.conditional_branches.p95) / $($phase.conditional_branches.spread) | $($phase.latency_ns.median) / $($phase.latency_ns.p95) / $($phase.latency_ns.spread) |"
    )
}
$markdown.Add('')
$markdown.Add('## Research gate')
$markdown.Add('')
$markdown.Add("- Correctness digests/results matched: yes")
$markdown.Add("- Query instruction improvement: $queryInstructionImprovement%")
$markdown.Add("- Query conditional-branch improvement: $queryBranchImprovement%")
$markdown.Add("- Query latency improvement: $queryLatencyImprovement%")
$markdown.Add("- Durable-save latency regression: $saveLatencyRegression%")
$markdown.Add("- Status: $($report.gate.status)")
$markdown.Add('- Default remains disabled; an eligible result permits a separate proposal, not automatic enablement.')
$markdown.Add('')
if ($hardwareCycles.status -ceq 'not-collected') {
    $markdown.Add('Hardware cycles were not collected. TCG instruction/branch counts are not described as CPU cycles.')
} else {
    $markdown.Add("Hardware cycles were collected separately on pinned host: $($hardwareCycles.profile.description)")
}
Write-Utf8NoBom $markdownPath (($markdown -join "`n") + "`n")

[pscustomobject]@{
    Passed = $true
    GateStatus = $report.gate.status
    DefaultEnabled = $false
    JsonReport = $jsonPath
    MarkdownReport = $markdownPath
}
