# Tasks: InferenceOS CUI/GUI Filesystem Demonstrator

**Input**: Design documents from `/specs/001-inferenceos/`

**Tests**: Mandatory under the specification and constitution. Test tasks precede implementation
within each story and must initially expose the missing behavior.

## Format: `[ID] [P?] [Story] Description`

- `[P]` means the task can run in parallel with other ready tasks because it edits independent files.
- `[USn]` maps directly to User Story n in `spec.md`.
- Every task names its primary file or directory.

## Phase 1: Setup

**Purpose**: Establish reproducible greenfield project, toolchain, and validation structure.

- [X] T001 Create an idempotent WSL Ubuntu bootstrap that installs and validates the exact R2 toolchain/VM version matrix, records those versions in `tools/bootstrap/versions.json`, rejects mismatched existing binaries unless it builds a project-local pinned copy, and emits a sourceable project environment file without modifying global shell profiles in `tools/bootstrap/wsl-ubuntu.ps1` and `tools/bootstrap/wsl-ubuntu.sh`
- [X] T002 Create the planned source, test, tool, and build directory skeleton under `src/`, `tests/`, `tools/`, and `cmake/`
- [X] T003 Create root CMake project and freestanding target defaults in `CMakeLists.txt` and `cmake/Freestanding.cmake`
- [X] T004 [P] Define GCC cross-compiler profile in `cmake/toolchains/x86_64-elf-gcc.cmake`
- [X] T005 [P] Define Clang/LLD cross-compiler profile in `cmake/toolchains/x86_64-none-elf-clang.cmake`
- [X] T006 Add configure/build/test presets for both compiler matrices in `CMakePresets.json`
- [X] T007 [P] Document compiler-extension allowlist and ownership rules in `docs/compiler-extensions.md`
- [X] T008 [P] Add project-owned source-root and forbidden-extension validation in `tools/test/validate_source_layout.ps1`
- [X] T009 Add reproducible artifact directories and ignore rules in `.gitignore`

---

## Phase 2: Foundational Architecture

**Purpose**: Blocking runtime and test mechanisms shared by every user story.

**Critical**: No user-story implementation begins until this phase passes both compilers.

- [X] T010 [P] Implement freestanding memory/string/runtime primitives in `src/runtime/memory.c`, `src/runtime/string.c`, and `src/runtime/include/inferenceos/runtime.h`
- [X] T011 [P] Define fixed-width base types, errors, assertions, and portability macros in `src/kernel/include/inferenceos/base.h`, `errors.h`, and `compiler.h`
- [X] T012 [P] Create x86-64 kernel and UEFI linker scripts in `src/arch/x86_64/kernel.ld` and `src/boot/uefi/loader.ld`
- [X] T013 Implement x86-64 kernel entry, descriptor tables, traps, and context transition in `src/arch/x86_64/entry.S`, `gdt.c`, and `interrupts.c`
- [X] T014 [P] Implement COM1 early diagnostics and panic path in `src/drivers/serial/serial.c` and `src/kernel/panic.c`
- [X] T015 Implement physical/virtual memory maps and page allocation in `src/kernel/memory/physical.c` and `src/kernel/memory/virtual.c`
- [X] T016 Implement ring-3 process identity, private address spaces, loader-supplied module descriptor validation, static ELF64 mapping, and ordered system-service startup in `src/kernel/process/process.c`, `src/kernel/process/loader.c`, and `src/boot/system_modules/descriptor.c`
- [X] T017 Implement ACPI MADT/PM-timer discovery, legacy PIC masking/remap, local-APIC timer calibration, 10 ms preemptive round-robin scheduling, fixed-priority kernel work, wait queues, idle execution, and process exit cleanup in `src/kernel/scheduler/scheduler.c`, `src/kernel/scheduler/wait_queue.c`, `src/arch/x86_64/acpi.c`, `pic.c`, and `apic_timer.c`
- [X] T018 Implement process-local generation handles and rights checks in `src/kernel/process/handle_table.c`
- [X] T019 Implement versioned syscall entry and safe copy-in/copy-out in `src/kernel/syscall/entry.S`, `dispatch.c`, and `usercopy.c`
- [X] T020 Implement bounded kernel IPC endpoints and trusted-service registration in `src/kernel/ipc/ipc.c`
- [X] T021 [P] Create host test support and assertions in `tests/support/test_main.c`, `tests/support/fake_block.c`, and `tests/support/fault_injector.c`
- [X] T022 [P] Add APIC discovery/calibration, 10 ms quantum, preemption, blocking/wakeup, starvation, priority, and lifecycle tests in `tests/unit/scheduler_test.c` and `tests/integration/process_lifecycle_test.c`
- [X] T023 Add dual-compiler foundational build tests and structure assertions in `tests/unit/runtime_test.c` and `tests/unit/layout_assertions_test.c`
- [X] T024 Implement minimum ESP, versioned SHA-256 system-module manifest, static application packaging, and sparse persistent-disk builders required by story tests in `tools/image/build_esp.ps1`, `tools/image/build_system_modules.ps1`, and `tools/image/create_persistent_disk.ps1`
- [X] T025 Add the explicit q35/OVMF/TCG launch profile and stable serial markers in `tools/test/run_inferenceos.ps1`
- [X] T026 Add the minimum headless QEMU story-test runner with timeouts and artifact retention in `tools/test/run_qemu_tests.ps1`
- [X] T027 Wire foundational `inferenceos-image`, `inferenceos-test-disk`, and QEMU test targets in `CMakeLists.txt`

**Checkpoint**: Kernel/runtime foundation compiles and host tests pass under GCC and Clang.

---

## Phase 3: User Story 1 - Boot CUI and Start GUI (Priority: P1) (MVP)

**Goal**: Boot through UEFI to an independently usable CUI, start the layered GUI, and recover to
CUI after graphical failure.

**Independent Test**: Twenty clean QEMU boots reach the prompt; twenty GUI starts reach a usable
desktop/terminal; injected GUI initialization failure leaves the prompt responsive.

### Tests for User Story 1 (MANDATORY)

- [X] T028 [P] [US1] Add UEFI handoff, system-module manifest/digest, overlap, required-role, and degraded-GUI validation tests in `tests/unit/boot_info_test.c` and `tests/unit/system_module_manifest_test.c`
- [X] T029 [P] [US1] Add CUI parser and shared-command contract tests in `tests/unit/cui_parser_test.c`
- [X] T030 [P] [US1] Add graphics, input, window, and GUI failure tests in `tests/integration/gui_runtime_test.c`
- [X] T031 [US1] Add 20-run boot/GUI/recovery QEMU suite in `tests/system/boot_gui_test.ps1`

### Implementation for User Story 1

- [X] T032 [US1] Implement PE32+ UEFI loader, kernel ELF64 validation, SHA-256 module-manifest parsing, required/optional module loading, memory-map capture, and GOP/module-descriptor handoff in `src/boot/uefi/main.c`, `src/boot/uefi/elf.c`, `src/boot/uefi/modules.c`, and `src/boot/uefi/sha256.c`
- [X] T033 [P] [US1] Implement PS/2 keyboard/mouse drivers and normalized events in `src/drivers/ps2/keyboard.c`, `mouse.c`, and `src/gui/input/events.c`
- [X] T034 [P] [US1] Implement framebuffer abstraction, primitives, and PSF2 text in `src/drivers/framebuffer/gop.c` and `src/gui/graphics/`
- [X] T035 [US1] Implement retained window manager, clipping, dirty composition, and pointer in `src/gui/window/window_manager.c` and `src/gui/window/compositor.c`
- [X] T036 [US1] Implement shared CUI command engine and standalone console in `src/cui/parser.c`, `src/cui/commands.c`, and `src/cui/console.c`
- [X] T037 [US1] Implement the minimal required Shell bootstrap process, desktop/terminal module startup, shared command-engine hosting, and CUI recovery teardown in `src/shell/bootstrap.c`, `src/gui/desktop/desktop.c`, and `src/gui/terminal/terminal.c`

**Checkpoint**: US1 is demonstrable without persistent filesystem support.

---

## Phase 4: User Story 2 - Format and Mount >=50 GB Volume (Priority: P1)

**Goal**: Discover a virtio-blk disk, format version-1 InferenceOS-FS, and mount it at `/`.

**Independent Test**: Format, validate capacity/geometry, mount, report free space, and reject
undersized or unsafe volumes using a sparse reference disk.

### Tests for User Story 2 (MANDATORY)

- [X] T038 [P] [US2] Add fixed-point FAT geometry and overflow property tests in `tests/unit/fs_geometry_test.c`
- [X] T039 [P] [US2] Add superblock encode/CRC/backup disagreement tests in `tests/unit/superblock_test.c`
- [X] T040 [P] [US2] Add virtio-blk completion/flush/error tests in `tests/integration/virtio_block_test.c`
- [X] T041 [US2] Add 50 GB format, mount, capacity/free-space, clean unmount, and remount state system test in `tests/system/format_mount_test.ps1`

### Implementation for User Story 2

- [X] T042 [US2] Implement PCI discovery and virtio-blk negotiation/request queues in `src/drivers/virtio_blk/virtio_blk.c`
- [X] T043 [US2] Implement generic block API and generation-based write-back cache in `src/block/device.c` and `src/block/cache.c`
- [X] T044 [P] [US2] Implement version-1 superblock codec and static layout assertions in `src/filesystems/inferenceos_fs/superblock.c` and `format.h`
- [X] T045 [US2] Implement geometry calculation and formatter in `src/filesystems/inferenceos_fs/formatter.c`
- [X] T046 [US2] Implement VFS mount registry, root namespace, and InferenceOS-FS mount states in `src/vfs/mount.c` and `src/filesystems/inferenceos_fs/mount.c`
- [X] T047 [US2] Implement `devices`, `diskinfo`, `format`, `mount`, `unmount`, and `fsinfo` handlers in `src/cui/fs_commands.c`

---

## Phase 5: User Story 3 - Durable File and Companion Pair (Priority: P1)

**Goal**: Persist each regular file as one authoritative primary plus one valid companion.

**Independent Test**: Create/save `REPORT.TXT`, verify exact records/hash, inject every persistence
failure, reboot, and observe either a valid pair or a detectable incomplete state.

### Tests for User Story 3 (MANDATORY)

- [X] T048 [P] [US3] Add 8.3 canonicalization, FNV-1a, CRC, and checksum vectors in `tests/unit/fs_metadata_test.c`
- [X] T049 [P] [US3] Add primary/companion codec and collision tests in `tests/unit/directory_record_test.c`
- [X] T050 [P] [US3] Add FAT allocation, loop, bounds, and ownership tests in `tests/unit/fat_chain_test.c`
- [X] T051 [US3] Add hosted save-order fault matrix and persisted-image record inspection in `tests/fault/file_commit_fault_test.c`

### Implementation for User Story 3

- [X] T052 [P] [US3] Implement primary, companion, hash, CRC, and association codecs in `src/filesystems/inferenceos_fs/records.c`, `hash.c`, and `crc32.c`
- [X] T053 [P] [US3] Implement validated FAT traversal/allocation/freeing in `src/filesystems/inferenceos_fs/fat.c`
- [X] T054 [US3] Implement paired directory scanning and slot allocation in `src/filesystems/inferenceos_fs/directory.c`
- [X] T055 [US3] Implement VFS file open/read/write/seek/append paths in `src/filesystems/inferenceos_fs/file.c`
- [X] T056 [US3] Implement uncommitted fencing and ordered create/save/rename/delete commits in `src/filesystems/inferenceos_fs/transaction.c`
- [X] T057 [US3] Implement `create`, `write`, `append`, `type`, `rename`, and `delete` handlers in `src/cui/file_commands.c`
- [X] T058 [US3] Integrate dirty tracking, `sync`, flush errors, and durable-result propagation in `src/filesystems/inferenceos_fs/sync.c`

---

## Phase 6: User Story 4 - Extension-Hidden File Explorer (Priority: P1)

**Goal**: Browse extension-free names and type icons without exposing companions or hashes.

**Independent Test**: File Explorer shows `REPORT`, handles hidden-name collisions, uses mapped or
generic icons, and leaks zero prohibited fields.

### Tests for User Story 4 (MANDATORY)

- [X] T059 [P] [US4] Add safe DTO and forbidden-field contract tests in `tests/contract/display_safe_entry_test.c`
- [X] T060 [P] [US4] Add hidden-name collision and icon fallback tests in `tests/unit/file_view_model_test.c`
- [X] T061 [US4] Add File Explorer rendering/input system test using an injectable fake display-safe view provider in `tests/system/file_explorer_test.ps1`

### Implementation for User Story 4

- [X] T062 [US4] Implement display-safe entry conversion and deterministic disambiguation in `src/shell/display_safe_entry.c`
- [X] T063 [P] [US4] Implement kernel-owned type/icon catalog in `src/kernel/process/type_catalog.c`
- [X] T064 [US4] Implement File Explorer model, injectable view-provider interface, widgets, navigation, and properties in `src/gui/file_explorer/model.c`, `view_provider.c`, `window.c`, and `properties.c`
- [X] T065 [US4] Route ordinary CUI `dir` through the shared display-safe model in `src/cui/directory_commands.c`

---

## Phase 7: User Story 5 - Shell-Brokered Search and Rendering (Priority: P1)

**Goal**: Make Shell IPC the observable mediation path for File Explorer services.

**Independent Test**: Trace directory, type, search, and GUI-view requests through Shell and prove
that neither Shell nor File Explorer reads raw filesystem structures.

### Tests for User Story 5 (MANDATORY)

- [X] T066 [P] [US5] Add Shell IPC version/bounds/restart contract tests in `tests/contract/shell_ipc_test.c`
- [X] T067 [P] [US5] Add VFS-bypass dependency validation in `tools/test/validate_dependencies.ps1`
- [X] T068 [US5] Add Shell/File Explorer trace integration test in `tests/integration/shell_file_explorer_test.c`

### Implementation for User Story 5

- [X] T069 [US5] Define versioned Shell message schemas in `src/shell/include/inferenceos/shell_protocol.h`
- [X] T070 [US5] Extend the trusted Shell bootstrap service with versioned file-view validation, restart behavior, and request dispatch in `src/shell/service.c`
- [X] T071 [P] [US5] Implement VFS-backed directory/type/search services in `src/kernel/syscall/file_view.c`
- [X] T072 [P] [US5] Implement GUI view/render request boundary in `src/kernel/syscall/gui_view.c`
- [X] T073 [US5] Connect File Explorer IPC client and refresh semantics in `src/gui/file_explorer/client.c`

---

## Phase 8: User Story 11 - Reboot Persistence and Recovery (Priority: P1)

**Goal**: Preserve acknowledged data and metadata across reboot and classify corruption safely.

**Independent Test**: Twenty save/sync/reboot cycles retain exact bytes; malformed disks select
read-write, diagnostic read-only, or rejected state without unsafe access.

### Tests for User Story 11 (MANDATORY)

- [X] T074 [P] [US11] Add exhaustive mount-state corruption tests in `tests/fault/mount_validation_test.c`
- [X] T075 [P] [US11] Add unmount/write/flush failure tests in `tests/fault/sync_unmount_test.c`
- [X] T076 [US11] Add a 20-cycle persistent-disk QEMU suite proving CUI-created files are readable from both the live GUI terminal and Shell-backed File Explorer, GUI-terminal-created/renamed files are readable from the standalone CUI, and all interfaces retain the same mounted namespace across reboot in `tests/system/reboot_persistence_test.ps1`

### Implementation for User Story 11

- [X] T077 [US11] Implement bounded ownership/loop scans and mount-state classification in `src/filesystems/inferenceos_fs/validator.c`
- [X] T078 [US11] Implement diagnostic read-only access and rejected-mount reporting in `src/filesystems/inferenceos_fs/diagnostic_mount.c`
- [x] T079 [US11] Implement operation draining, sync refusal, and cache invalidation on unmount in `src/vfs/unmount.c`
- [x] T080 [US11] Integrate ordered `reboot` and `shutdown` with filesystem/device flush in `src/kernel/power.c`

---

## Phase 9: User Story 7 - Proprietary Application Filtering (Priority: P2)

**Goal**: Return only trusted application-bound file types through opaque capabilities.

**Independent Test**: Mixed files yield only registered types; collisions are verified; fabricated
type capabilities and cross-process handles grant nothing.

### Tests for User Story 7 (MANDATORY)

- [x] T081 [P] [US7] Add application-binding and forged-capability tests in `tests/contract/proprietary_routing_test.c`
- [x] T082 [US7] Add proprietary viewer integration test in `tests/integration/proprietary_viewer_test.c`

### Implementation for User Story 7

- [x] T083 [US7] Implement trusted application identity/type bindings in `src/kernel/process/application_bindings.c`
- [x] T084 [US7] Implement process-scoped type capability minting and verification in `src/kernel/process/type_capability.c`
- [x] T085 [US7] Implement Shell proprietary enumeration and content-handle flow in `src/shell/proprietary_service.c`
- [x] T086 [US7] Implement proprietary test viewer in `src/applications/proprietary_test/main.c`

---

## Phase 10: User Story 8 - Custom Application Approved APIs (Priority: P2)

**Goal**: Deny raw type discovery while supporting approved proprietary adapters.

**Independent Test**: Raw extension/hash/arbitrary-type requests fail; approved adapter operations
succeed on opaque content handles; absent adapters produce explicit unsupported results.

### Tests for User Story 8 (MANDATORY)

- [x] T087 [P] [US8] Add custom-application denial and adapter contract tests in `tests/contract/custom_application_test.c`
- [x] T088 [US8] Add end-to-end approved adapter test in `tests/integration/proprietary_adapter_test.c`

### Implementation for User Story 8

- [x] T089 [US8] Define versioned proprietary adapter contract in `src/shell/include/inferenceos/proprietary_adapter.h`
- [x] T090 [US8] Implement adapter registration/invocation with rights reduction in `src/shell/adapter_service.c`
- [x] T091 [US8] Implement custom test application and denied-query probes in `src/applications/custom_test/main.c`

---

## Phase 11: User Story 9 - Privileged Diagnostics (Priority: P2)

**Goal**: Observe internal records and chains safely through explicit diagnostic authority.

**Independent Test**: Authorized CUI/GUI inspectors correlate records/hash/chain and report injected
faults; ordinary paths remain unchanged and leak-free.

### Tests for User Story 9 (MANDATORY)

- [x] T092 [P] [US9] Add diagnostic capability separation tests in `tests/contract/diagnostic_authority_test.c`
- [x] T093 [P] [US9] Add bounded malformed-record/chain diagnostic tests in `tests/fault/diagnostic_bounds_test.c`

### Implementation for User Story 9

- [x] T094 [US9] Implement privileged filesystem diagnostic service in `src/kernel/syscall/fs_diagnostic.c`
- [x] T095 [US9] Implement `fileinfo`, `hashinfo`, `fatinfo`, and expanded `fsinfo` in `src/cui/diagnostic_commands.c`
- [x] T096 [P] [US9] Implement GUI diagnostic inspector widgets in `src/gui/file_explorer/diagnostic_inspector.c`
- [x] T097 [US9] Add serial-safe structured diagnostic formatting in `src/filesystems/inferenceos_fs/diagnostics.c`

---

## Phase 12: User Story 10 - Shared Hierarchical Directories (Priority: P2)

**Goal**: Create and navigate directories coherently from CUI and GUI.

**Independent Test**: Create `/DOCS`, traverse `.`/`..`, grow directory storage, share visibility,
reject root escape and non-empty removal, and expose each regular pair once.

### Tests for User Story 10 (MANDATORY)

- [x] T098 [P] [US10] Add VFS path normalization/root-boundary tests in `tests/unit/vfs_path_test.c`
- [x] T099 [P] [US10] Add directory growth/pair-boundary tests in `tests/integration/directory_growth_test.c`
- [x] T100 [US10] Add cross-CUI/GUI directory lifecycle test in `tests/system/directory_interop_test.ps1`

### Implementation for User Story 10

- [x] T101 [US10] Implement absolute/relative path resolution and current directories in `src/vfs/path.c`
- [x] T102 [US10] Implement subdirectory `.`/`..`, growth, enumeration, and removal in `src/filesystems/inferenceos_fs/subdirectory.c`
- [x] T103 [US10] Implement VFS mkdir/rmdir/list/rename coherence in `src/vfs/directory.c`
- [x] T104 [US10] Implement `cd`, `pwd`, `mkdir`, and `rmdir` CUI handlers in `src/cui/directory_commands.c`
- [x] T105 [US10] Connect File Explorer navigation/create/delete operations in `src/gui/file_explorer/controller.c`

---

## Phase 13: User Story 12 - Reproducible Source Build (Priority: P2)

**Goal**: Build, package, launch, and validate the demonstrator from a clean checkout.

**Independent Test**: Both compilers pass their matrix; primary artifacts boot from a generated ESP
with a generated sparse disk using one documented command sequence.

### Tests for User Story 12 (MANDATORY)

- [x] T106 [P] [US12] Add complete GCC/Clang clean-build CI workflows in `.github/workflows/build.yml`
- [x] T107 [P] [US12] Add bootstrap idempotency/global-profile-safety, pinned-version, module-manifest, and artifact reproducibility tests in `tests/system/bootstrap_test.ps1` and `tests/system/artifact_manifest_test.ps1`
- [X] T108 [US12] Extend the foundational QEMU runner with the full release matrix and evidence manifest in `tools/test/run_qemu_tests.ps1`

### Implementation for User Story 12

- [X] T109 [US12] Add deterministic image manifests and reproducibility checks to `tools/image/build_esp.ps1`, `tools/image/create_persistent_disk.ps1`, and `tools/image/write_manifest.ps1`
- [X] T110 [US12] Validate and document host-portable behavior of the foundational q35/OVMF/TCG profile in `tests/system/qemu_profile_test.ps1`
- [X] T111 [P] [US12] Document clean build, tool versions, launch, limitations, and licensing in `docs/build.md`, `docs/limitations.md`, and `LICENSE`
- [X] T112 [US12] Wire aggregate release-validation and artifact-manifest targets in `CMakeLists.txt`

---

## Phase 14: User Story 6 - Research-Gated Extension Registry (Priority: P3)

**Goal**: Implement optional derived registry behavior and measure it without affecting correctness.

**Independent Test**: Enabled mode creates or refreshes one record per extension, supplies validated
Shell-backed File Explorer type views, exposes authorized non-mutating diagnostics, and rebuilds or
falls back safely after stale-state reboot; matched benchmarks produce the predefined report, and
disabled mode passes all mandatory tests.

### Tests for User Story 6 (MANDATORY)

- [X] T113 [P] [US6] Add registry codec/create/refresh/rebuild and authorized non-mutating diagnostic tests in `tests/unit/extension_registry_test.c` and `tests/contract/registry_diagnostics_test.c`
- [X] T114 [P] [US6] Add disabled/full/stale/corrupt and post-reboot fallback tests proving registry state cannot override authoritative records in `tests/fault/registry_fallback_test.c`
- [X] T115 [US6] Add Shell-backed File Explorer enabled-registry type-view integration coverage and a matched corpus/query benchmark harness in `tests/integration/registry_file_view_test.c` and `tests/benchmarks/registry_benchmark.c`

### Implementation for User Story 6

- [X] T116 [US6] Implement version-1 registry record codec and validation in `src/filesystems/inferenceos_fs/registry_record.c`
- [X] T117 [US6] Implement default-off refresh/rebuild/fallback registry service in `src/filesystems/inferenceos_fs/registry.c`
- [X] T118 [US6] Integrate optional registry updates without making commits authoritative in `src/filesystems/inferenceos_fs/transaction.c`
- [X] T119 [US6] Implement authorized, non-mutating registry diagnostics and runtime enablement control in `src/filesystems/inferenceos_fs/registry_diagnostics.c`
- [X] T120 [US6] Implement TCG marker collection and benchmark report generation in `tools/test/run_registry_benchmark.ps1`

---

## Phase 15: Polish and Cross-Cutting Validation

**Purpose**: Prove the complete constitutional release claim and finish documentation.

- [X] T121 [P] Run and document static dependency/VFS-boundary enforcement in `tools/test/validate_dependencies.ps1`
- [X] T122 [P] Add full SC-001-SC-020 traceability report generator in `tools/test/generate_validation_report.ps1`
- [ ] T123 Run the complete dual-compiler, host, integration, fault, and QEMU matrix and archive evidence under `build/validation/`
- [X] T124 [P] Document InferenceOS-FS format, extension hash, and recovery states in `docs/inferenceos-fs.md`
- [X] T125 [P] Document CUI commands, GUI architecture, and application contracts in `docs/cui.md`, `docs/gui.md`, and `docs/applications.md`
- [ ] T126 Validate the clean-checkout workflow against `specs/001-inferenceos/quickstart.md`
- [ ] T127 Perform final Constitution Check and release-claim audit in `docs/validation/constitution-check.md`

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: starts immediately.
- **Foundational (Phase 2)**: depends on Setup and blocks every story.
- **P1 path**: US1 -> US2 -> US3 -> US4 -> US5 -> US11. US4 uses an injectable fake view provider for its independent checkpoint; US5 replaces it with live Shell IPC.
- **P2 path**: US7/US8 require US5; US9 requires US3; US10 requires US3, US4, and US5; US12 can advance after the foundational build/image contracts stabilize.
- **P3 registry**: US6 requires US3, US5, and US11 so fallback and persistence baselines exist.
- **Polish**: requires every included story.

### User Story Dependencies

```text
Foundational
  |-- US12 reproducible build foundation (final validation tracks all stories)
  `-- US1 Boot/CUI/GUI
       `-- US2 Block/format/mount
            `-- US3 file + companion durability
                 |-- US9 diagnostics
                 `-- US4 hidden File Explorer
                      `-- US5 Shell mediation
                           |-- US7 proprietary filtering -> US8 custom adapters
                           |-- US10 directories
                           `-- US11 reboot/recovery -> US6 registry research
```

### Constitution Traceability

| Constitution gate | Implementation tasks | Validation tasks |
|---|---|---|
| CUI/GUI and recovery | T032-T037 | T028-T031, T123 |
| Shared VFS/storage layering | T042-T047, T069-T073 | T038-T041, T066-T068, T121 |
| Distinct records/collision safety | T052-T058 | T048-T051 |
| Hidden metadata/application mediation | T062-T065, T069-T073, T083-T091 | T059-T061, T066-T068, T081-T088 |
| Durability and capacity | T043-T058, T077-T080 | T038-T041, T048-T051, T074-T076 |
| Registry non-authority/research gate | T116-T120 | T113-T115 |
| GUI/input layering | T033-T037 | T030-T031, T121 |
| C17/dual compiler/minimal assembly | T003-T027, T109-T112 | T001, T004-T008, T022-T023, T106-T108, T123 |
| Release claims | T124-T127 | T122-T123 |

## Parallel Opportunities

- Setup compiler profiles, documentation, and validation scripts can run in parallel after T002.
- In Foundational, runtime/types, linker scripts, serial, and host test support can run in parallel.
- Within every story, `[P]` tests target separate files and can be authored concurrently before implementation.
- After US3, US4, US9, and much of US12 can proceed concurrently.
- After US5, US7 and US10 can proceed concurrently; US11 follows the completed live GUI/Shell path.

## Parallel Examples

- **US1**: T028, T029, and T030 in parallel; T033 and T034 in parallel after boot contracts exist.
- **US2**: T038, T039, and T040 in parallel; T044 can proceed alongside T042.
- **US3**: T048, T049, and T050 in parallel; T052 and T053 in parallel.
- **US4**: T059 and T060 in parallel; T063 can proceed alongside T062.
- **US5**: T066 and T067 in parallel; T071 and T072 in parallel after protocol definition.
- **US11**: T074 and T075 in parallel before the persistence system suite.
- **US7/US8**: contract tests can be prepared in parallel after the Shell contract stabilizes.
- **US9**: T092 and T093 in parallel; CUI and GUI inspectors can proceed in parallel.
- **US10**: T098 and T099 in parallel; CUI and GUI controllers can proceed after VFS operations.
- **US12**: CI, artifact tests, and documentation can proceed in parallel.
- **US6**: T113 and T114 in parallel; codec implementation precedes service and benchmark integration.

## Implementation Strategy

### MVP First

1. Complete Setup and Foundational phases.
2. Complete US1 to demonstrate boot, standalone CUI, GUI desktop, input, window, and recovery.
3. Validate the US1 checkpoint before adding persistent storage.

### Incremental Delivery

1. Add US2 and US3 for the defining persistent filesystem and companion experiment.
2. Add US4, US5, and US11 to complete the P1 user-visible and durability path.
3. Add P2 application, diagnostic, directory, and reproducibility stories.
4. Add US6 last because the registry is optional and research-gated.
5. Run cross-cutting release validation only after each story passes independently.

## Notes

- Do not mark implementation tasks complete merely because code compiles; the corresponding test
  and independent-story checkpoint must pass.
- Keep the Extension Registry disabled by default unless T115/T120 satisfy the predefined gate.
- Any required on-disk layout change must return to specification and constitution review before code.

---

## Phase 16: Convergence

**Purpose**: Close the final-system gaps exposed by T123. The existing host-tested components are
retained; these tasks connect them into the bootable q35/OVMF demonstrator required by the
specification and Constitution.

- [X] T128 CRITICAL Implement and link the freestanding `kernel.elf`, including `kernel_main` boot-information validation, early failure diagnostics, memory/platform initialization, and the transition from `src/arch/x86_64/entry.S` into the kernel runtime in `src/kernel/main.c`, `src/arch/x86_64/kernel.ld`, and `CMakeLists.txt` (missing; Constitution I and XI, User Story 1 acceptance criterion 1, FR-002, FR-016, FR-020, FR-257, SC-001, SC-020)
- [X] T129 CRITICAL Complete runnable ring-3 process scheduling by preserving/restoring user contexts across timer interrupts, entering each loaded process at its ELF entry point with its private stack/address space, resuming blocked processes, and terminating through the syscall path in `src/arch/x86_64/context.S`, `src/arch/x86_64/entry.S`, `src/kernel/scheduler/scheduler.c`, and `src/kernel/process/process.c` (partial; Constitution I, plan Technical Context execution model, FR-027 through FR-040, and the implementation claims of T013, T016, and T017)
- [X] T130 CRITICAL Implement the concrete q35 virtio-blk PCI transport—device enumeration, capability/BAR validation and mapping, feature negotiation, DMA-safe virtqueues, request completion, flush, timeout/error handling, and publication through the generic block API—in `src/arch/x86_64/pci.c`, `src/drivers/virtio_blk/transport_pci.c`, and `src/drivers/virtio_blk/virtio_blk.c` (partial; Constitution II and V, User Story 2 acceptance criterion 1, FR-043, FR-054 through FR-064, SC-003, and the implementation claim of T042)
- [X] T131 CRITICAL Add the live kernel composition root that initializes interrupts and PS/2 input, storage/cache/VFS/InferenceOS-FS, the complete shared CUI command registry, Shell IPC services, desktop/terminal/File Explorer, diagnostics, and synchronized q35 reboot/shutdown while preserving CUI recovery on GUI failure in `src/kernel/runtime.c`, `src/cui/`, `src/shell/`, and the affected driver/service adapters (missing; Constitution I, II, VII, VIII, and X, User Stories 1, 2, 4, 5, and 11, FR-016 through FR-030, FR-042 through FR-044, FR-179, SC-001 through SC-004)
- [X] T132 CRITICAL Create the freestanding user runtime and separately linked static ELF64 system-application images for Shell, GUI desktop, GUI terminal, File Explorer, proprietary test, and custom test, with CRT entry points plus versioned syscall/IPC client glue and no direct filesystem-driver linkage, in `src/runtime/user/`, `src/applications/`, `src/shell/`, `src/gui/`, and `CMakeLists.txt` (missing; Constitution I through IV, plan Technical Context application model, FR-027 through FR-040, FR-217 through FR-224, and SC-011 through SC-014)
- [X] T133 HIGH Generate the versioned `system_modules.json`, hashes, symbols/maps, licensed PSF2 font asset, and complete ESP directly from the kernel and application targets so `inferenceos-image` has no external prebuilt kernel/module inputs, updating `CMakeLists.txt`, `tools/image/build_system_modules.ps1`, `tools/image/build_esp.ps1`, and the documented clean-build workflow (partial; Constitution VIII and XI, plan Primary Dependencies, FR-232, FR-254 through FR-258, SC-018 through SC-020, and the implementation claims of T024, T027, T109, and T112)
- [ ] T134 HIGH Implement the guest-side q35 test-control protocol and stable serial evidence markers for boot/GUI recovery, input, format/mount, file and directory workflows, persistence cycles, fault injection, and registry benchmark counters, then bind the existing release suites to those real guest actions in `src/kernel/test_control.c`, the affected runtime services, `tools/test/run_inferenceos.ps1`, and `tools/test/run_qemu_tests.ps1` (missing; Constitution XI, FR-239 through FR-253, SC-001 through SC-020, and T123)
