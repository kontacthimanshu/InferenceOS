# Tasks: InferenceOS Minimal InferenceFS-FAT32 Demonstrator

**Input**: Design documents from `specs/001-InferenceOS/`

**Prerequisites**: `plan.md`, `spec.md`, `research.md`, `data-model.md`, `contracts/`, `quickstart.md`

**Tests**: Required by FR-162–FR-175 and the user-story acceptance scenarios. Write each story's tests first and confirm they fail for the intended reason before implementation.

**Organization**: Tasks are grouped by user story. `[P]` means the task can proceed in parallel because it targets different files and has no dependency on another incomplete task in the same group.

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Establish the repository layout and reproducible build/test entry points.

- [x] T001 Create the planned production, tool, documentation, and test directory structure with ownership notes in docs/architecture/project-layout.md
- [x] T002 Configure GCC 16.2 freestanding C17 reference builds and GNU binutils 2.45 in cmake/toolchains/gcc-x86_64-elf.cmake
- [x] T003 [P] Configure Clang/LLVM 22.1.8 freestanding C17 validation builds in cmake/toolchains/clang-x86_64-none-elf.cmake
- [x] T004 [P] Define common warnings-as-errors, no-red-zone, freestanding, deterministic-path, and extension-allowlist flags in cmake/InferenceCompilerPolicy.cmake
- [x] T005 Define GCC/Clang configure, build, host-test, QEMU, persistence, corruption, and fault presets in CMakePresets.json
- [x] T006 Implement exact tool/firmware version and SHA-256 capture in tools/generate-release-manifest.py
- [x] T007 [P] Add deterministic archive, timestamp, source-path, volume-ID, and artifact-checksum policy in docs/build/reproducibility.md
- [x] T008 [P] Document the approved C/assembly extension allowlist and ABI boundaries in docs/build/compiler-extensions.md

**Checkpoint**: Both compiler profiles configure from a clean checkout and reject unapproved language modes/extensions.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Implement shared contracts, runtime primitives, and test infrastructure required by every story.

**CRITICAL**: No user-story implementation begins until this phase passes under both compiler profiles.

- [x] T009 Define fixed-width types, alignment/section macros, result codes, assertions, and checked arithmetic in include/inferenceos/base.h and include/inferenceos/result.h
- [x] T010 [P] Implement freestanding memcpy, memmove, memset, memcmp, and bounded string helpers in lib/memory.c and lib/string.c
- [x] T011 [P] Implement explicit little-endian load/store helpers in include/inferenceos/endian.h
- [x] T012 [P] Implement FNV-1a-32 and CRC-32/ISO-HDLC primitives in lib/fnv1a.c and lib/crc32.c
- [x] T013 [P] Implement primary-name checksum and 8.3 canonicalization primitives in fs/inferencefs/name.c
- [x] T014 Define exact superblock, primary-record, and companion-record layouts plus `_Static_assert` size/offset gates in include/inferencefs/format.h
- [x] T015 Define opaque generic block-device operations, status, geometry, and error contract in include/inferenceos/block_device.h
- [x] T016 Define fixed 64-entry sector-cache API and ordering/error states in include/inferenceos/block_cache.h
- [x] T017 Define opaque mount/node/file/directory handles, VFS operations, path limits, and status taxonomy in include/inferenceos/vfs.h
- [x] T018 [P] Define typed read-only filesystem diagnostic responses in include/inferencefs/diagnostics.h
- [x] T019 Implement physical-page allocation and bounded kernel heap/fixed pools in kernel/memory/page_allocator.c and kernel/memory/heap.c
- [x] T020 [P] Implement COM1 serial output and bounded panic reporting in drivers/console/serial.c and kernel/panic/panic.c
- [x] T021 [P] Implement memory-backed block device with operation logging and deterministic failure injection in tests/support/memory_block_device.c
- [x] T022 [P] Create the host-native C17 test runner and assertion utilities in tests/support/test_main.c and tests/support/test_assert.h
- [x] T023 Add dual-compiler unit tests for runtime, endian, checked arithmetic, hash, CRC, checksum, and filename vectors in tests/unit/test_primitives.c and tests/unit/test_names.c
- [x] T024 Implement fixed-capacity cache lookup, pinning, deterministic replacement, dirty tracking, ordered flush, and failure retention in block/cache/block_cache.c
- [x] T025 Add memory-device tests for cache hits, dirty eviction, ordering, bounds, write failures, and flush failures in tests/unit/test_block_cache.c
- [x] T026 Implement bounded component path normalization for `/`, `.`, `..`, 255 bytes, and 16 levels in vfs/path.c
- [x] T027 Add path normalization and root-escape tests in tests/unit/test_vfs_path.c

**Checkpoint**: Shared primitives compile freestanding and natively; all foundational host tests pass under GCC and Clang.

---

## Phase 3: User Story 1 — Boot to an Interactive Prompt (Priority: P1) — MVP

**Goal**: Boot the documented one-vCPU UEFI QEMU profile to `InferenceOS>`, accept keyboard input, run `help`, and recover from malformed commands.

**Independent Test**: Launch a clean boot image and confirm banner/prompt, printable ASCII, Backspace, Enter, `help`, unknown-command recovery, and early serial diagnostics.

### Tests for User Story 1

- [x] T028 [P] [US1] Create headless QEMU/OVMF process, serial transcript, timeout, and QMP keyboard harness in tests/integration/qemu_harness.py
- [x] T029 [P] [US1] Define boot milestone, prompt, line-editing, help, unknown-command, and panic transcript tests in tests/integration/test_boot_prompt.py

### Implementation for User Story 1

- [x] T030 [P] [US1] Implement BOOTX64.EFI loader, checked ELF loading, UEFI memory-map capture, GOP handoff, and ExitBootServices flow in boot/uefi/loader.c
- [x] T031 [P] [US1] Implement isolated Microsoft-x64-to-SysV UEFI ABI trampoline and kernel entry shim in boot/uefi/abi_trampoline.S and arch/x86_64/entry.S
- [x] T032 [US1] Define reviewed fixed-address ELF64 kernel sections and exported bounds in arch/x86_64/kernel.ld
- [x] T033 [P] [US1] Implement GDT, IDT, exception stubs, CPU halt/reboot primitives, and assembly boundary comments in arch/x86_64/cpu.c and arch/x86_64/exceptions.S
- [x] T034 [P] [US1] Implement GOP framebuffer character console and serial fan-out in drivers/console/framebuffer.c and drivers/console/console.c
- [x] T035 [P] [US1] Implement bounded polling i8042/PS2 keyboard decoding for ASCII, Backspace, and Enter in drivers/input/ps2_keyboard.c
- [x] T036 [US1] Implement the 256-byte line editor, strict tokenizer, static command registry, `help`, `version`, `clear`, and deterministic errors in shell/shell.c and shell/commands/core.c
- [x] T037 [US1] Wire boot information, memory, exceptions, consoles, input, and shell startup in kernel/init/kernel_main.c
- [x] T038 [US1] Package the standard FAT32 UEFI boot image and launch target in cmake/BootImage.cmake and tools/run-qemu.py
- [ ] T039 [US1] Run and pass the boot/prompt suite including 20 consecutive clean boots via tests/integration/test_boot_prompt.py

**Checkpoint**: US1 is a bootable, interactive, independently demonstrable MVP.

---

## Phase 4: User Story 2 — Format and Mount Through VFS (Priority: P1)

**Goal**: Detect the ATA disk, format InferenceFS-FAT32 v1, validate it, and mount it at `/` through VFS.

**Independent Test**: Attach a blank supported disk; run `format`, `mount`, and `fsinfo`; reject invalid signature/version/geometry without writable mount.

### Tests for User Story 2

- [x] T040 [P] [US2] Add ATA identification, sector I/O, timeout, bounds, status, and flush tests to tests/integration/test_ata_device.py
- [x] T041 [P] [US2] Add geometry boundary, overflow, formatter layout, superblock CRC, and FAT initialization tests to tests/unit/test_format_mount.c
- [x] T042 [P] [US2] Add VFS mount-state, opaque-handle, and command-boundary contract tests to tests/unit/test_vfs_mount.c
- [x] T043 [P] [US2] Add valid, backup-only, differing, corrupt, unsupported, nonzero-reserved-field, unsupported-attribute, cross-linked-chain, and impossible-geometry mount images to tests/corruption/test_superblocks.py

### Implementation for User Story 2

- [x] T044 [P] [US2] Implement ATA IDENTIFY, bounded LBA28 PIO read/write, error decoding, timeouts, and CACHE FLUSH in drivers/storage/ata_pio/ata_pio.c
- [x] T045 [US2] Register the fixed PIIX IDE disk only through the generic block-device API in block/device/device_registry.c
- [x] T046 [P] [US2] Implement checked v1 geometry solver and formatter write ordering in fs/inferencefs/format.c
- [x] T047 [P] [US2] Implement independent superblock decoding/CRC validation and safe geometry derivation in fs/inferencefs/superblock.c
- [x] T048 [US2] Implement the shared read-only FAT walker, typed directory-slot parser, exact companion encode/decode/validation codec, and clean-writable/diagnostic-read-only/rejected full-namespace mount validator with cluster ownership/cross-link detection in fs/inferencefs/fat.c, fs/inferencefs/directory.c, fs/inferencefs/companion.c, fs/inferencefs/validator.c, and fs/inferencefs/mount.c
- [ ] T049 [US2] Implement single-root lifecycle, InferenceFS operation-table registration, and the minimal correct filesystem-to-cache-to-device `sync`/`unmount` flush path in vfs/vfs.c, fs/inferencefs/vfs_adapter.c, and fs/inferencefs/sync.c
- [ ] T050 [US2] Implement `devices`, `diskinfo`, `format`, `mount`, `unmount`, `fsinfo`, and `sync` handlers in shell/commands/storage.c
- [ ] T051 [US2] Run and pass blank-format, valid-mount, full-namespace corruption rejection, minimal sync/unmount failure propagation, fsinfo, and VFS-boundary scenarios in tests/integration/test_format_mount.py

**Checkpoint**: A fresh disk can be formatted and mounted only through the validated VFS/filesystem/block stack.

---

## Phase 5: User Story 3 — Complete Persistent File Lifecycle (Priority: P1)

**Goal**: Create, write, append, read, list, base-rename, and delete files with safe allocation and no-space behavior.

**Independent Test**: Complete the `TEST.TXT` lifecycle, including multi-cluster I/O, rebooted content verification, deletion, cluster reuse, and disk-full failure.

### Tests for User Story 3

- [ ] T052 [P] [US3] Add FAT get/set/allocation/free, fragmentation, loop/range, zeroing, and full-volume tests in tests/unit/test_fat.c
- [ ] T053 [P] [US3] Add typed directory scan, adjacent-pair allocation, end/deleted slots, cluster-boundary, and duplicate-name tests in tests/unit/test_directory.c
- [ ] T054 [P] [US3] Add VFS create/open/read/write/seek/append/list/base-rename/delete tests, including rejection of writes starting beyond EOF and failure before exceeding `0xFFFFFFFF` bytes, in tests/unit/test_file_lifecycle.c
- [ ] T055 [P] [US3] Add QEMU multi-cluster lifecycle, no-space, and cluster-reuse scenarios in tests/integration/test_file_lifecycle.py

### Implementation for User Story 3

- [ ] T056 [P] [US3] Extend the US2 read-only FAT walker with checked writes, rotating allocation hint, zero-before-link allocation, chain linking, and validated free in fs/inferencefs/fat.c
- [ ] T057 [P] [US3] Extend the US2 typed directory parser with reusable-slot mutation, two-slot same-cluster allocation, and directory-chain extension in fs/inferencefs/directory.c
- [ ] T058 [US3] Implement regular-file entry-set creation using the US2 companion codec with uncommitted-companion/primary/committed-companion ordering in fs/inferencefs/file.c
- [ ] T059 [US3] Implement byte read, write, append, checked seek, rejection of writes starting beyond EOF, preflight rejection above `0xFFFFFFFF` bytes, cluster growth, zero initialization, and publish-size-last behavior in fs/inferencefs/file.c
- [ ] T060 [US3] Implement directory enumeration, base-only rename checksum/CRC update, and hide-before-free deletion in fs/inferencefs/directory.c
- [ ] T061 [US3] Connect file and directory operations to opaque VFS handles in fs/inferencefs/vfs_adapter.c
- [ ] T062 [US3] Implement `dir`, `create`, `write`, `append`, `type`, `rename`, and `delete` handlers in shell/commands/files.c
- [ ] T063 [US3] Run and pass all file lifecycle, fragmentation, multi-cluster, no-space, and reuse tests in tests/integration/test_file_lifecycle.py

**Checkpoint**: Regular files have a complete independently testable lifecycle through VFS.

---

## Phase 6: User Story 4 — Demonstrate the Extension-Hash Companion (Priority: P1)

**Goal**: Create and validate the defining companion record and prove hash collision safety.

**Independent Test**: Inspect `TEST.TXT`, same-extension and different-extension files, empty/1/2/3-byte extensions, and a forced collision while preserving exact-name identity.

### Tests for User Story 4

- [ ] T064 [P] [US4] Add golden companion layout, algorithm ID, flags, checksum, CRC, empty/1/2/3-byte extension, and corruption vectors in tests/unit/test_companion.c
- [ ] T065 [P] [US4] Add deterministic hash-collision prefilter and authoritative-name comparison tests in tests/unit/test_hash_collision.c
- [ ] T066 [P] [US4] Add QEMU same/different-extension and lowercase-canonicalization scenarios in tests/integration/test_hash_companion.py

### Implementation for User Story 4

- [ ] T067 [P] [US4] Extend the shared US2 companion codec with typed corruption provenance and independent stored-versus-recomputed hash/checksum/CRC verification for diagnostics in fs/inferencefs/companion.c
- [ ] T068 [US4] Integrate companion validation into directory scanning and regular-file visibility in fs/inferencefs/directory.c
- [ ] T069 [US4] Implement hash-prefilter lookup followed by authoritative extension/full-name verification in fs/inferencefs/lookup.c
- [ ] T070 [US4] Implement validated companion diagnostic extraction and `hashinfo` output in fs/inferencefs/diagnostics.c and shell/commands/diagnostics.c
- [ ] T071 [US4] Run and pass companion, canonicalization, corruption, and forced-collision suites in tests/integration/test_hash_companion.py

**Checkpoint**: The extension-hash experiment is directly observable and collision-safe.

---

## Phase 7: User Story 5 — Rename Across Extensions (Priority: P1)

**Goal**: Rename files across extensions while consistently recomputing every derived companion field.

**Independent Test**: Rename `TEST.TXT` to `TEST.LOG`, verify unchanged data and recomputed length/hash/checksum/CRC; verify destination collision and interrupted rename behavior.

### Tests for User Story 5

- [ ] T072 [P] [US5] Add base-only, extension-changing, destination-exists, and injected-phase rename tests in tests/unit/test_rename.c
- [ ] T073 [P] [US5] Add QEMU rename metadata/content and interruption scenarios in tests/integration/test_rename.py

### Implementation for User Story 5

- [ ] T074 [US5] Implement hide-update-recommit rename state machine with destination checks and phase barriers in fs/inferencefs/rename.c
- [ ] T075 [US5] Integrate extension-change recalculation and base-only hash preservation into fs/inferencefs/companion.c
- [ ] T076 [US5] Propagate rename inconsistency/I/O outcomes through VFS and command diagnostics in fs/inferencefs/vfs_adapter.c and shell/commands/files.c
- [ ] T077 [US5] Run and pass successful, conflicting, and every injected-boundary rename scenario in tests/integration/test_rename.py

**Checkpoint**: Rename never presents a half-updated pair as a successfully committed file.

---

## Phase 8: User Story 6 — Persistence Across Reboot (Priority: P1)

**Goal**: Flush, reboot, remount, and revalidate acknowledged file content and companion metadata.

**Independent Test**: Create multiple files, sync, reboot, remount, and compare content/hash diagnostics before and after; repeat mutation cycles twenty times.

### Tests for User Story 6

- [ ] T078 [P] [US6] Add filesystem→cache→device flush ordering and failed-durability tests in tests/unit/test_sync.c
- [ ] T079 [P] [US6] Add sync/unmount/reboot/shutdown transcript and on-disk revalidation tests in tests/integration/test_persistence.py
- [ ] T080 [P] [US6] Add 20-cycle mutation/sync/reboot persistence runner in tests/integration/test_persistence_cycles.py

### Implementation for User Story 6

- [ ] T081 [US6] Extend the US2 flush path with operation-defined mutation barriers, dirty/error retention, and durability evidence in fs/inferencefs/sync.c
- [ ] T082 [US6] Extend VFS unmount with outstanding-operation busy/refusal behavior and handle-generation invalidation in vfs/vfs.c
- [ ] T083 [US6] Route `sync`, `unmount`, `reboot`, and `shutdown` through the identical required flush sequence in shell/commands/storage.c and shell/commands/system.c
- [ ] T084 [US6] Revalidate companion records and chains from disk on every remount in fs/inferencefs/mount.c
- [ ] T085 [US6] Run and pass orderly reboot, unmount/remount, failed-flush, and 20-cycle persistence suites in tests/integration/test_persistence.py

**Checkpoint**: Acknowledged data and hash metadata demonstrably persist as on-disk state.

---

## Phase 9: User Story 7 — Inspect File, Hash, and Allocation (Priority: P2)

**Goal**: Expose validated file records, companion metadata, and bounded cluster chains from the prompt.

**Independent Test**: Run `fileinfo`, `hashinfo`, and `fatinfo` on empty, multi-cluster, and corrupt files without unbounded or out-of-range access.

### Tests for User Story 7

- [ ] T086 [P] [US7] Add typed diagnostic contract tests for empty, valid, multi-cluster, and corrupt objects in tests/unit/test_diagnostics.c
- [ ] T087 [P] [US7] Add QEMU diagnostic field and bounded-corruption transcript tests in tests/integration/test_diagnostics.py

### Implementation for User Story 7

- [ ] T088 [US7] Implement bounded file/record/hash/chain diagnostic queries in fs/inferencefs/diagnostics.c
- [ ] T089 [US7] Implement `fileinfo` and `fatinfo` formatting plus shared bounded diagnostic formatting without duplicating US4 `hashinfo` in shell/commands/diagnostics.c
- [ ] T090 [US7] Run and pass valid, empty, fragmented, and corrupt diagnostic scenarios in tests/integration/test_diagnostics.py

**Checkpoint**: The on-disk experiment can be understood without a debugger or host hex editor.

---

## Phase 10: User Story 8 — Create and Navigate Directories (Priority: P2)

**Goal**: Provide a bounded hierarchical namespace with `.`/`..`, directory expansion, and safe removal.

**Independent Test**: Create `/DOCS/NOTE.TXT`, navigate absolute/relative paths, prove root confinement, expand a directory, and reject non-empty removal.

### Tests for User Story 8

- [ ] T091 [P] [US8] Add directory initialization, dot entries, expansion, parent/root, non-empty removal, and depth tests in tests/unit/test_directories.c
- [ ] T092 [P] [US8] Add QEMU `/DOCS` create/navigate/list/remove scenario in tests/integration/test_directories.py

### Implementation for User Story 8

- [ ] T093 [US8] Implement directory cluster allocation, zeroing, `.`/`..`, expansion, empty checking, and removal in fs/inferencefs/directory.c
- [ ] T094 [US8] Implement VFS current-directory resolution and directory handles in vfs/path.c and fs/inferencefs/vfs_adapter.c
- [ ] T095 [US8] Implement `cd`, `pwd`, `mkdir`, and `rmdir` handlers in shell/commands/directories.c
- [ ] T096 [US8] Run and pass hierarchy, depth, root-confinement, expansion, and non-empty removal tests in tests/integration/test_directories.py

**Checkpoint**: A small hierarchy works through VFS without companion misuse.

---

## Phase 11: User Story 9 — Detect Malformed Metadata Safely (Priority: P2)

**Goal**: Deterministically reject or diagnose every required superblock, companion, directory, and FAT corruption class.

**Independent Test**: Boot each crafted image and verify clean, diagnostic-read-only, or rejected state without out-of-volume I/O, uncontrolled traversal, or automatic repair.

### Tests for User Story 9

- [ ] T097 [P] [US9] Implement deterministic read-only image parser/mutator with optional CRC repair in tools/inferencefs_image.py
- [ ] T098 [P] [US9] Generate missing/orphan/duplicate companion, version/algorithm/CRC/checksum/hash, and inconsistent-superblock fixtures in tests/corruption/generate_cases.py
- [ ] T099 [P] [US9] Generate FAT loop, bad/reserved/out-of-range, short/overlong chain, cross-link, and impossible-geometry fixtures in tests/corruption/generate_fat_cases.py
- [ ] T100 [P] [US9] Define mount/diagnostic result and no-out-of-range-I/O assertions for every fixture in tests/corruption/test_corruption_matrix.py

### Implementation for User Story 9

- [ ] T101 [US9] Extend the US2 namespace validator with typed corruption provenance for duplicate/ambiguous entry sets, size/chain mismatches, and unsupported records in fs/inferencefs/validator.c
- [ ] T102 [US9] Map every crafted corruption class to stable diagnostic-read-only or rejected mount outcomes without weakening US2 writable-mount safety in fs/inferencefs/mount.c
- [ ] T103 [US9] Expose stable clean/read-only/rejected corruption diagnostics through shell/commands/storage.c
- [ ] T104 [US9] Run and pass the full corruption matrix while proving no out-of-volume block request in tests/corruption/test_corruption_matrix.py

**Checkpoint**: Corrupt metadata is never silently accepted as a healthy writable filesystem.

---

## Phase 12: User Story 10 — Rebuild and Run from Source (Priority: P2)

**Goal**: Let an external developer reproduce artifacts and run the complete demonstration from a clean checkout.

**Independent Test**: Use only repository documentation and pinned tools to perform GCC and Clang builds, launch QEMU, and complete the mandatory scenario.

### Tests for User Story 10

- [ ] T105 [P] [US10] Add clean-tree dual-compiler and unapproved-extension CI checks in tests/integration/test_build_profiles.py
- [ ] T106 [P] [US10] Add two-clean-build artifact comparison and manifest verification in tests/integration/test_reproducibility.py
- [ ] T107 [P] [US10] Add assembly-location and forbidden-filesystem-policy scan in tests/integration/test_architecture_policy.py

### Implementation for User Story 10

- [ ] T108 [P] [US10] Implement one documented clean build/test/image/launch workflow in tools/build.ps1 and tools/build.sh
- [ ] T109 [P] [US10] Document v1 byte layouts, algorithms, and independent image parsing in docs/format/inferencefs-fat32-v1.md
- [ ] T110 [P] [US10] Document all commands, expected diagnostics, and mandatory demonstration in docs/commands/reference.md
- [ ] T111 [P] [US10] Document supported host/tool versions, QEMU topology, OVMF inputs, limitations, and troubleshooting in docs/build/getting-started.md and docs/limitations/v1.md
- [ ] T112 [US10] Generate deterministic boot/data images, symbols, launch metadata, and release manifest from the top-level workflow in CMakeLists.txt
- [ ] T113 [US10] Run and pass clean-checkout build, dual-compiler, reproducibility, architecture-policy, and documented-demo tests in tests/integration/test_reproducibility.py

**Checkpoint**: The filesystem claim is independently buildable, runnable, and auditable.

---

## Phase 13: Polish and Cross-Cutting Quality Gates

**Purpose**: Complete mandated interruption testing, documentation consistency, and release acceptance across stories.

- [ ] T114 [P] Add a test-only ordinal/LBA read-write-flush decorator implementing the real block contract in tests/support/fault_block_device.c and exclude it from release targets in cmake/InferenceTests.cmake
- [ ] T115 [P] Add interrupted companion creation, primary creation, rename, delete, FAT extension, and flush scenarios in tests/fault/test_mutation_boundaries.py
- [ ] T116 Run every fault boundary and assert only valid-old, valid-new, detectably incomplete, diagnostic-only, or rejected outcomes in tests/fault/test_mutation_boundaries.py
- [ ] T117 [P] Audit all fixed on-disk structures and accessors against spec offsets in docs/format/layout-audit.md
- [ ] T118 [P] Audit every command’s ordinary file access for strict VFS mediation in docs/architecture/vfs-boundary-audit.md
- [ ] T119 [P] Reconcile README claims with the proof-of-concept scope, derived-hash semantics, and collision limitations in README.md
- [ ] T120 Run the complete quickstart and record the mandatory demonstration transcript in docs/validation/mandatory-demo.txt
- [ ] T121 Run 20 boot and 20 mutation/sync/reboot cycles and record results/tool manifest checksums in docs/validation/release-report.md
- [ ] T122 Verify every FR-001–FR-175 and SC-001–SC-015 has implementation/test evidence in docs/validation/requirements-traceability.md

---

## Dependencies and Execution Order

### Phase Dependencies

- Phase 1 Setup has no dependency.
- Phase 2 Foundation depends on Setup and blocks every user story.
- US1 establishes the running kernel/prompt and is the MVP.
- US2 depends on US1 for interactive demonstration and on Foundation for block/VFS contracts; it establishes the shared read-only FAT walker, typed directory parser, and companion codec required for safe writable mount.
- US3 depends on US2's formatted mounted filesystem and extends US2 parsing/validation primitives with allocation and mutation rather than replacing them.
- US4 depends on the US2 companion codec and US3 entry-set creation; it adds diagnostic-grade verification and observable hash behavior without reimplementing encoding or decoding.
- US5 depends on US4 companion validation and US3 rename/file primitives.
- US6 depends on US3–US5 mutation paths and cache/device flush.
- US7 depends on US3–US4 data structures; it can proceed in parallel with US6 after those prerequisites.
- US8 depends on US2 VFS/mount and US3 directory/FAT primitives; it can proceed in parallel with US4–US7 once those primitives exist.
- US9 depends on US2's writable-mount safety validator and US3–US5 structure validators; it adds exhaustive corruption provenance and fixture coverage rather than deferring baseline mount safety.
- US10 depends on the desired demonstration stories; its documentation and CI scaffolding can proceed earlier.
- Phase 13 depends on all selected user stories.

### User Story Completion Order

```text
Setup → Foundation → US1 → US2 → US3
                              |--→ US4 → US5 → US6
                              |            |--→ US7
                              |--→ US8     `--→ US9
                              `----------------→ US10
All selected stories → Polish/Release Gates
```

### Within Each Story

1. Add tests and verify they fail for the intended missing behavior.
2. Implement lower-level models/primitives before adapters and commands.
3. Run host contract tests before QEMU integration tests.
4. Complete the independent-test checkpoint before treating the story as done.

## Parallel Execution Examples

- **US1**: T030 loader, T033 CPU exceptions, T034 console, and T035 keyboard target different files after boot contracts exist.
- **US2**: T044 ATA, T046 formatter, and T047 superblock decoder can proceed in parallel behind their contracts.
- **US3**: T056 FAT and T057 directory iterator can proceed in parallel; T058–T062 follow their integration.
- **US4**: T064–T066 tests can proceed together; T067 diagnostic companion verification and T069 lookup are separable before directory integration.
- **US5**: T072 unit tests and T073 QEMU scenarios can be authored together before T074–T076.
- **US6**: T078–T080 can be authored in parallel; sync/VFS/system handlers then integrate sequentially.
- **US7**: T086 and T087 can be authored in parallel before the diagnostic implementation.
- **US8**: T091 and T092 can be authored in parallel before directory/VFS/command integration.
- **US9**: T097–T100 can generate independent corruption categories in parallel.
- **US10**: T108–T111 build workflow and documentation target distinct files and can proceed in parallel.

## Implementation Strategy

### MVP First

1. Complete Setup and Foundation.
2. Complete US1 through T039.
3. Stop and validate boot, keyboard, prompt, `help`, unknown-command recovery, and serial panic output.

This is the smallest runnable increment, but it does not yet demonstrate the filesystem.

### Filesystem Demonstration Increment

Complete US2 → US3 → US4 → US5 → US6. This is the first increment that satisfies the constitution’s core persistent extension-hash demonstration.

### Full First Release

Add US7–US10, then complete Phase 13. Do not claim release readiness until T122 maps every requirement and success criterion to passing evidence.

## Notes

- `[P]` indicates different-file work with no incomplete dependency in the same group; shared-file integration tasks are intentionally sequential.
- Every task includes an exact file path and can be executed without inventing a new architectural boundary.
- Commit after each task or coherent test/implementation pair.
- Hashes are prefilters only; canonical extension and full primary name remain authoritative.
- Do not use `ms_abi` compiler annotations without first amending the approved extension allowlist.
