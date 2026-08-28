<#
.SYNOPSIS
Generates deterministic SC-001 through SC-020 traceability reports.

.DESCRIPTION
Reads the authoritative success criteria from specs/001-inferenceos/spec.md, verifies
that every mapped source and CTest entry exists, discovers retained CTest/QEMU/report
evidence, and writes JSON plus Markdown traceability reports. Missing evidence is
reported honestly; it is not treated as a successful validation run.

.PARAMETER RequireComplete
Returns a failing exit code unless all twenty success criteria have passing evidence.
#>
[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '../..'),
    [string]$EvidenceDirectory,
    [string]$OutputDirectory,
    [switch]$RequireComplete
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Test-Property([object]$Object, [string]$Name) {
    return $null -ne $Object -and $null -ne $Object.PSObject.Properties[$Name]
}

function Get-RelativeEvidencePath([string]$Base, [string]$Path) {
    $basePrefix = [IO.Path]::GetFullPath($Base).TrimEnd(
        [IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($basePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Evidence path escaped its root: $fullPath"
    }
    return $fullPath.Substring($basePrefix.Length).Replace('\', '/')
}

function New-Trace([string]$Id, [string[]]$ImplementationTasks,
    [string[]]$ValidationTasks, [string[]]$Sources, [string[]]$CTestNames,
    [string[]]$QemuSuites, [string[]]$Artifacts, [string[]]$Commands) {
    [pscustomobject][ordered]@{
        Id = $Id
        ImplementationTasks = [string[]]$ImplementationTasks
        ValidationTasks = [string[]]$ValidationTasks
        Sources = [string[]]$Sources
        CTestNames = [string[]]$CTestNames
        QemuSuites = [string[]]$QemuSuites
        Artifacts = [string[]]$Artifacts
        Commands = [string[]]$Commands
    }
}

$root = [IO.Path]::GetFullPath($RepositoryRoot)
$specificationPath = Join-Path $root 'specs/001-inferenceos/spec.md'
$tasksPath = Join-Path $root 'specs/001-inferenceos/tasks.md'
foreach ($requiredPath in @($specificationPath, $tasksPath, (Join-Path $root 'CMakeLists.txt'),
    (Join-Path $root 'tests/CMakeLists.txt'))) {
    if (-not [IO.File]::Exists($requiredPath)) { throw "Required traceability input is missing: $requiredPath" }
}
$evidenceRoot = if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
    Join-Path $root 'build/validation'
} else { [IO.Path]::GetFullPath($EvidenceDirectory) }
$outputRoot = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $evidenceRoot
} else { [IO.Path]::GetFullPath($OutputDirectory) }
[IO.Directory]::CreateDirectory($evidenceRoot) | Out-Null
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null

$specification = [IO.File]::ReadAllText($specificationPath)
$criterionMatches = [regex]::Matches(
    $specification, '(?m)^-\s+[*][*](?<id>SC-[0-9]{3})[*][*]:\s*(?<text>[^\r\n]+?)\s*$'
)
$criteria = [ordered]@{}
foreach ($match in $criterionMatches) {
    $id = $match.Groups['id'].Value
    if ($criteria.Contains($id)) { throw "Duplicate success criterion in specification: $id" }
    $criteria[$id] = $match.Groups['text'].Value
}
$expectedIds = @(1..20 | ForEach-Object { 'SC-{0:D3}' -f $_ })
if ($criteria.Count -ne $expectedIds.Count -or
    @($expectedIds | Where-Object { -not $criteria.Contains($_) }).Count -ne 0 -or
    @($criteria.Keys | Where-Object { $_ -notin $expectedIds }).Count -ne 0) {
    throw 'The specification must define exactly SC-001 through SC-020.'
}

$mandatoryRegistryOffTests = @(
    'power-unit', 'vfs-path-unit', 'vfs-directory-integration', 'fs-geometry-unit',
    'superblock-unit', 'fs-metadata-unit', 'directory-record-unit', 'directory-growth-integration',
    'fat-chain-unit', 'file-unit', 'file-commit-fault', 'ps2-input-unit', 'graphics-unit',
    'window-manager-unit', 'cui-parser-unit', 'gui-runtime-integration', 'shell-bootstrap-integration',
    'virtio-block-integration', 'block-cache-integration', 'fs-sync-integration',
    'display-safe-entry-contract', 'proprietary-routing-contract', 'custom-application-contract',
    'diagnostic-authority-contract', 'diagnostic-bounds-fault', 'shell-ipc-contract',
    'proprietary-viewer-integration', 'proprietary-adapter-integration', 'file-view-model-unit',
    'shell-file-explorer-integration', 'file-view-service-integration',
    'gui-view-service-integration', 'file-explorer-client-integration',
    'registry-file-view-integration', 'mount-integration', 'mount-validation-fault',
    'registry-fallback-fault', 'sync-unmount-fault', 'fs-commands-integration',
    'dependency-validation'
)
$hostCommands = @('ctest --preset gcc-host', 'ctest --preset clang-host')
$trace = @(
    New-Trace 'SC-001' @('T013-T017', 'T032', 'T036') @('T022', 'T028-T031', 'T108', 'T123') `
        @('tests/unit/boot_info_test.c', 'tests/unit/system_module_manifest_test.c', 'tests/unit/cui_parser_test.c', 'tests/system/boot_gui_test.ps1') `
        @('boot-info-unit', 'system-module-manifest-unit', 'cui-parser-unit') @('boot-gui-recovery') @() `
        ($hostCommands + 'cmake --build --preset gcc-debug --target test-boot')
    New-Trace 'SC-002' @('T033-T037') @('T030-T031', 'T121', 'T123') `
        @('tests/unit/ps2_input_test.c', 'tests/unit/graphics_test.c', 'tests/unit/window_manager_test.c', 'tests/integration/gui_runtime_test.c', 'tests/system/boot_gui_test.ps1') `
        @('ps2-input-unit', 'graphics-unit', 'window-manager-unit', 'gui-runtime-integration') @('boot-gui-recovery') @('dependency-boundaries') `
        ($hostCommands + 'cmake --build --preset gcc-debug --target test-gui')
    New-Trace 'SC-003' @('T042-T047', 'T058', 'T077-T080') @('T038-T041', 'T074-T076', 'T123') `
        @('tests/unit/fs_geometry_test.c', 'tests/integration/mount_test.c', 'tests/integration/fs_sync_test.c', 'tests/system/format_mount_test.ps1', 'tests/system/reboot_persistence_test.ps1') `
        @('fs-geometry-unit', 'mount-integration', 'fs-sync-integration', 'fs-commands-integration') @('format-mount', 'reboot-persistence') @() `
        @('ctest --preset gcc-integration', 'ctest --preset clang-integration', 'cmake --build --preset gcc-debug --target test-format-mount')
    New-Trace 'SC-004' @('T055-T058', 'T079-T080') @('T051', 'T075-T076', 'T123') `
        @('tests/fault/file_commit_fault_test.c', 'tests/fault/sync_unmount_test.c', 'tests/system/reboot_persistence_test.ps1') `
        @('file-commit-fault', 'sync-unmount-fault', 'fs-sync-integration') @('reboot-persistence') @() `
        @('ctest --preset gcc-fault', 'ctest --preset clang-fault', 'cmake --build --preset gcc-debug --target test-reboot-persistence')
    New-Trace 'SC-005' @('T052-T056') @('T049-T051', 'T076', 'T123') `
        @('tests/unit/directory_record_test.c', 'tests/unit/file_test.c', 'tests/fault/file_commit_fault_test.c') `
        @('directory-record-unit', 'file-unit', 'file-commit-fault') @('reboot-persistence') @() $hostCommands
    New-Trace 'SC-006' @('T052') @('T048-T049', 'T123') `
        @('tests/unit/fs_metadata_test.c', 'tests/unit/directory_record_test.c') `
        @('fs-metadata-unit', 'directory-record-unit') @() @() $hostCommands
    New-Trace 'SC-007' @('T052', 'T062-T063', 'T071', 'T083-T085') @('T049', 'T059-T060', 'T081-T082', 'T123') `
        @('tests/unit/directory_record_test.c', 'tests/integration/file_view_service_test.c', 'tests/contract/proprietary_routing_test.c') `
        @('directory-record-unit', 'file-view-service-integration', 'proprietary-routing-contract') @() @() $hostCommands
    New-Trace 'SC-008' @('T062-T073', 'T083-T091') @('T059-T061', 'T066', 'T068', 'T081-T088', 'T115', 'T123') `
        @('tests/contract/display_safe_entry_test.c', 'tests/unit/file_view_model_test.c', 'tests/contract/custom_application_test.c', 'tests/integration/shell_file_explorer_test.c', 'tests/integration/registry_file_view_test.c', 'tests/system/file_explorer_test.ps1') `
        @('display-safe-entry-contract', 'file-view-model-unit', 'custom-application-contract', 'shell-file-explorer-integration', 'registry-file-view-integration') @('file-explorer') @('dependency-boundaries') `
        @('ctest --preset gcc-contract', 'ctest --preset clang-contract', 'cmake --build --preset gcc-debug --target test-file-explorer')
    New-Trace 'SC-009' @('T094-T097', 'T119') @('T092-T093', 'T113', 'T123') `
        @('tests/contract/diagnostic_authority_test.c', 'tests/fault/diagnostic_bounds_test.c', 'tests/contract/registry_diagnostics_test.c', 'tests/unit/file_explorer_diagnostic_inspector_test.c') `
        @('diagnostic-authority-contract', 'diagnostic-bounds-fault', 'registry-diagnostics-contract', 'file-explorer-diagnostic-inspector-unit') @() @() $hostCommands
    New-Trace 'SC-010' @('T062-T073', 'T101-T105') @('T061', 'T068', 'T076', 'T100', 'T115', 'T123') `
        @('tests/integration/shell_file_explorer_test.c', 'tests/integration/file_explorer_client_test.c', 'tests/system/reboot_persistence_test.ps1', 'tests/system/directory_interop_test.ps1') `
        @('shell-file-explorer-integration', 'file-explorer-client-integration') @('reboot-persistence', 'directory-interop') @() `
        @('ctest --preset gcc-integration', 'ctest --preset clang-integration', 'cmake --build --preset gcc-debug --target test-directory-interop')
    New-Trace 'SC-011' @('T062', 'T064-T065') @('T059-T061', 'T123') `
        @('tests/unit/file_view_model_test.c', 'tests/integration/file_view_service_test.c', 'tests/system/file_explorer_test.ps1') `
        @('file-view-model-unit', 'file-view-service-integration') @('file-explorer') @() $hostCommands
    New-Trace 'SC-012' @('T063-T064') @('T060-T061', 'T123') `
        @('tests/unit/file_view_model_test.c', 'tests/system/file_explorer_test.ps1') `
        @('file-view-model-unit') @('file-explorer') @() $hostCommands
    New-Trace 'SC-013' @('T083-T086') @('T081-T082', 'T123') `
        @('tests/contract/proprietary_routing_test.c', 'tests/integration/proprietary_viewer_test.c') `
        @('proprietary-routing-contract', 'proprietary-viewer-integration') @() @() $hostCommands
    New-Trace 'SC-014' @('T089-T091') @('T087-T088', 'T123') `
        @('tests/contract/custom_application_test.c', 'tests/integration/proprietary_adapter_test.c') `
        @('custom-application-contract', 'proprietary-adapter-integration') @() @() $hostCommands
    New-Trace 'SC-015' @('T117-T119') @('T113-T115', 'T120', 'T123') `
        @('tests/fault/registry_fallback_test.c', 'tests/integration/registry_file_view_test.c', 'tools/test/run_qemu_tests.ps1') `
        $mandatoryRegistryOffTests @('boot-gui-recovery', 'format-mount', 'file-explorer', 'reboot-persistence', 'directory-interop') @('dependency-boundaries') `
        @('ctest --preset gcc-host', 'ctest --preset clang-host', 'ctest --preset gcc-integration', 'ctest --preset clang-integration', 'ctest --preset gcc-fault', 'ctest --preset clang-fault', 'ctest --preset gcc-contract', 'ctest --preset clang-contract')
    New-Trace 'SC-016' @('T116-T119') @('T113-T115', 'T123') `
        @('tests/unit/extension_registry_test.c', 'tests/integration/registry_file_view_test.c', 'tests/fault/registry_fallback_test.c') `
        @('extension-registry-unit', 'registry-file-view-integration', 'registry-fallback-fault') @() @() $hostCommands
    New-Trace 'SC-017' @('T115', 'T120') @('T115', 'T120', 'T123') `
        @('tests/benchmarks/registry_benchmark.c', 'tests/system/registry_benchmark_report_test.ps1', 'tools/test/run_registry_benchmark.ps1') `
        @('registry-benchmark-contract', 'registry-benchmark-report-system') @() @('registry-benchmark-report') `
        @('cmake --build --preset gcc-debug --target benchmark-registry-qemu')
    New-Trace 'SC-018' @('T052-T058', 'T077-T078') @('T049-T051', 'T074', 'T093', 'T123') `
        @('tests/unit/directory_record_test.c', 'tests/fault/file_commit_fault_test.c', 'tests/fault/mount_validation_test.c', 'tests/fault/diagnostic_bounds_test.c') `
        @('directory-record-unit', 'file-commit-fault', 'mount-validation-fault', 'diagnostic-bounds-fault') @() @() `
        @('ctest --preset gcc-fault', 'ctest --preset clang-fault')
    New-Trace 'SC-019' @('T044-T046', 'T053', 'T077-T078') @('T038-T040', 'T050', 'T074', 'T093', 'T123') `
        @('tests/unit/fs_geometry_test.c', 'tests/unit/fat_chain_test.c', 'tests/fault/mount_validation_test.c', 'tests/fault/diagnostic_bounds_test.c') `
        @('fs-geometry-unit', 'fat-chain-unit', 'mount-validation-fault', 'diagnostic-bounds-fault') @() @() `
        @('ctest --preset gcc-fault', 'ctest --preset clang-fault')
    New-Trace 'SC-020' @('T001-T009', 'T024-T027', 'T032-T037', 'T042-T047', 'T052-T058', 'T062-T080', 'T108-T112') @('T106-T112', 'T121-T127') `
        @('docs/build.md', 'specs/001-inferenceos/quickstart.md', 'tests/system/bootstrap_test.ps1', 'tests/system/artifact_manifest_test.ps1', 'tools/test/run_qemu_tests.ps1') `
        @('bootstrap-system', 'artifact-manifest-system', 'dependency-validation') @('boot-gui-recovery', 'format-mount', 'file-explorer', 'reboot-persistence', 'directory-interop') @('dependency-boundaries', 'quickstart-validation') `
        @('cmake --build --preset gcc-debug --target inferenceos-release-validation')
)

if ($trace.Count -ne 20 -or @($trace.Id | Sort-Object -Unique).Count -ne 20) {
    throw 'Traceability mapping must contain exactly one entry for SC-001 through SC-020.'
}

$cmakeText = [IO.File]::ReadAllText((Join-Path $root 'CMakeLists.txt')) + "`n" +
    [IO.File]::ReadAllText((Join-Path $root 'tests/CMakeLists.txt'))
$declaredCTestNames = @([regex]::Matches(
    $cmakeText, '(?is)add_test\s*\(\s*NAME\s+(?<name>[A-Za-z0-9_.+-]+)'
) | ForEach-Object { $_.Groups['name'].Value } | Sort-Object -Unique)
foreach ($mapping in $trace) {
    foreach ($source in $mapping.Sources) {
        if (-not [IO.File]::Exists((Join-Path $root $source))) {
            throw "$($mapping.Id) references a missing validation source: $source"
        }
    }
    foreach ($testName in $mapping.CTestNames) {
        if ($testName -notin $declaredCTestNames) {
            throw "$($mapping.Id) references an undeclared CTest: $testName"
        }
    }
}

$evidenceRecords = [Collections.Generic.List[object]]::new()
function Add-Evidence([string]$Selector, [string]$Status, [string]$Path) {
    $evidenceRecords.Add([pscustomobject][ordered]@{
        selector = $Selector
        status = $Status
        path = Get-RelativeEvidencePath $evidenceRoot $Path
    })
}

foreach ($file in @(Get-ChildItem -LiteralPath $evidenceRoot -Recurse -File -Filter '*.xml' |
    Sort-Object FullName)) {
    try { [xml]$document = [IO.File]::ReadAllText($file.FullName) } catch { continue }
    $relative = Get-RelativeEvidencePath $evidenceRoot $file.FullName
    $matrix = if ($relative -match '(?i)(^|[/_.-])clang($|[/_.-])') { 'clang' }
        elseif ($relative -match '(?i)(^|[/_.-])gcc($|[/_.-])') { 'gcc' } else { 'unknown' }
    foreach ($case in @($document.SelectNodes('//testcase'))) {
        $status = if ($null -ne $case.SelectSingleNode('./failure|./error')) { 'failed' }
            elseif ($null -ne $case.SelectSingleNode('./skipped')) { 'incomplete' } else { 'passed' }
        Add-Evidence "ctest:${matrix}:$($case.name)" $status $file.FullName
    }
}

foreach ($file in @(Get-ChildItem -LiteralPath $evidenceRoot -Recurse -File -Filter 'evidence-manifest.json' |
    Sort-Object FullName)) {
    try { $manifest = [IO.File]::ReadAllText($file.FullName) | ConvertFrom-Json } catch { continue }
    if (-not (Test-Property $manifest 'Matrix') -or
        $manifest.Matrix -cne 'primary-toolchain-qemu-release' -or
        -not (Test-Property $manifest 'Suites')) { continue }
    foreach ($suite in @($manifest.Suites)) {
        if (-not (Test-Property $suite 'Name') -or -not (Test-Property $suite 'Status')) { continue }
        $dryRun = (Test-Property $manifest 'DryRun') -and [bool]$manifest.DryRun
        $status = if ($dryRun -or $suite.Status -ceq 'planned') { 'incomplete' }
            elseif ($suite.Status -ceq 'passed') { 'passed' } else { 'failed' }
        Add-Evidence "qemu:$($suite.Name)" $status $file.FullName
    }
}

foreach ($file in @(Get-ChildItem -LiteralPath $evidenceRoot -Recurse -File -Filter 'dependency-boundaries.json' |
    Sort-Object FullName)) {
    try { $report = [IO.File]::ReadAllText($file.FullName) | ConvertFrom-Json } catch { continue }
    Add-Evidence 'artifact:dependency-boundaries' `
        $(if ((Test-Property $report 'status') -and $report.status -ceq 'passed') { 'passed' } else { 'failed' }) $file.FullName
}
foreach ($file in @(Get-ChildItem -LiteralPath $evidenceRoot -Recurse -File -Filter 'registry-benchmark-report.json' |
    Sort-Object FullName)) {
    try { $report = [IO.File]::ReadAllText($file.FullName) | ConvertFrom-Json } catch { continue }
    $valid = (Test-Property $report 'schema_version') -and $report.schema_version -eq 1 -and
        (Test-Property $report 'measurement_source') -and
        $report.measurement_source -ceq 'qemu-tcg-markers' -and
        (Test-Property $report 'phases') -and (Test-Property $report 'gate')
    Add-Evidence 'artifact:registry-benchmark-report' $(if ($valid) { 'passed' } else { 'failed' }) $file.FullName
}
foreach ($file in @(Get-ChildItem -LiteralPath $evidenceRoot -Recurse -File -Filter 'quickstart-validation.json' |
    Sort-Object FullName)) {
    try { $report = [IO.File]::ReadAllText($file.FullName) | ConvertFrom-Json } catch { continue }
    $passed = ((Test-Property $report 'Passed') -and [bool]$report.Passed) -or
        ((Test-Property $report 'status') -and $report.status -ceq 'passed')
    Add-Evidence 'artifact:quickstart-validation' $(if ($passed) { 'passed' } else { 'failed' }) $file.FullName
}

function Resolve-Evidence([string]$Selector) {
    $records = @($evidenceRecords | Where-Object { $_.selector -ceq $Selector } |
        Sort-Object path, status)
    if ($records.Count -eq 0) {
        return [pscustomobject][ordered]@{ selector = $Selector; status = 'not-collected'; paths = @() }
    }
    $status = if (@($records | Where-Object { $_.status -ceq 'failed' }).Count -ne 0) { 'failed' }
        elseif (@($records | Where-Object { $_.status -ne 'passed' }).Count -ne 0) { 'incomplete' }
        else { 'passed' }
    return [pscustomobject][ordered]@{
        selector = $Selector
        status = $status
        paths = @($records.path | Sort-Object -Unique)
    }
}

$taskText = [IO.File]::ReadAllText($tasksPath)
$taskSummary = [ordered]@{
    completed = @([regex]::Matches($taskText, '(?m)^- \[[xX]\] T[0-9]{3}\b')).Count
    pending = @([regex]::Matches($taskText, '(?m)^- \[ \] T[0-9]{3}\b')).Count
}
$criterionReports = foreach ($mapping in $trace) {
    $selectors = [Collections.Generic.List[string]]::new()
    foreach ($testName in $mapping.CTestNames) {
        $selectors.Add("ctest:gcc:$testName")
        $selectors.Add("ctest:clang:$testName")
    }
    foreach ($suite in $mapping.QemuSuites) { $selectors.Add("qemu:$suite") }
    foreach ($artifact in $mapping.Artifacts) { $selectors.Add("artifact:$artifact") }
    $resolved = @($selectors | Sort-Object -Unique | ForEach-Object { Resolve-Evidence $_ })
    $status = if (@($resolved | Where-Object { $_.status -ceq 'failed' }).Count -ne 0) { 'failed' }
        elseif (@($resolved | Where-Object { $_.status -eq 'passed' }).Count -eq 0) { 'not-collected' }
        elseif (@($resolved | Where-Object { $_.status -ne 'passed' }).Count -ne 0) { 'incomplete' }
        else { 'passed' }
    [pscustomobject][ordered]@{
        id = $mapping.Id
        outcome = $criteria[$mapping.Id]
        status = $status
        implementationTasks = $mapping.ImplementationTasks
        validationTasks = $mapping.ValidationTasks
        validationSources = $mapping.Sources
        commands = $mapping.Commands
        evidence = $resolved
    }
}

$summary = [ordered]@{ total = 20; passed = 0; failed = 0; incomplete = 0; notCollected = 0 }
foreach ($criterion in $criterionReports) {
    switch ($criterion.status) {
        'passed' { ++$summary.passed }
        'failed' { ++$summary.failed }
        'incomplete' { ++$summary.incomplete }
        default { ++$summary.notCollected }
    }
}
$report = [ordered]@{
    schemaVersion = 1
    report = 'inferenceos-success-criteria-traceability'
    specification = 'specs/001-inferenceos/spec.md'
    criteriaRange = 'SC-001-SC-020'
    taskSummary = $taskSummary
    summary = $summary
    criteria = @($criterionReports)
}
$jsonPath = Join-Path $outputRoot 'validation-traceability.json'
$markdownPath = Join-Path $outputRoot 'validation-traceability.md'
Write-Utf8NoBom $jsonPath (($report | ConvertTo-Json -Depth 12) + "`n")

$markdown = [Collections.Generic.List[string]]::new()
$markdown.Add('# InferenceOS SC-001-SC-020 Validation Traceability')
$markdown.Add('')
$markdown.Add("Criteria: 20 | Passed: $($summary.passed) | Failed: $($summary.failed) | Incomplete: $($summary.incomplete) | Not collected: $($summary.notCollected)")
$markdown.Add('')
$markdown.Add('| Criterion | Status | Measurable outcome |')
$markdown.Add('|---|---|---|')
foreach ($criterion in $criterionReports) {
    $outcome = $criterion.outcome.Replace('|', '\|')
    $markdown.Add("| $($criterion.id) | $($criterion.status) | $outcome |")
}
foreach ($criterion in $criterionReports) {
    $markdown.Add('')
    $markdown.Add("## $($criterion.id) - $($criterion.status)")
    $markdown.Add('')
    $markdown.Add($criterion.outcome)
    $markdown.Add('')
    $markdown.Add("- Implementation tasks: $($criterion.implementationTasks -join ', ')")
    $markdown.Add("- Validation tasks: $($criterion.validationTasks -join ', ')")
    $markdown.Add("- Sources: $($criterion.validationSources -join ', ')")
    $markdown.Add("- Commands: $($criterion.commands -join '; ')")
    $markdown.Add('- Evidence:')
    foreach ($item in $criterion.evidence) {
        $paths = if ($item.paths.Count -eq 0) { 'none' } else { $item.paths -join ', ' }
        $markdown.Add("  - $($item.selector): $($item.status) ($paths)")
    }
}
Write-Utf8NoBom $markdownPath (($markdown -join "`n") + "`n")

Write-Host "Validation traceability JSON: $jsonPath"
Write-Host "Validation traceability Markdown: $markdownPath"
Write-Host "SC status: passed=$($summary.passed) failed=$($summary.failed) incomplete=$($summary.incomplete) not-collected=$($summary.notCollected)"
if ($RequireComplete -and $summary.passed -ne 20) { exit 1 }
exit 0
