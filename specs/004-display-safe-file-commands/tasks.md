# Tasks: Display-Safe File Commands

**Input**: Design documents from `/specs/004-display-safe-file-commands/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: Validation is mandatory and is written before each implementation slice.

**Organization**: Tasks are grouped by user story while shared resolver and VFS identity infrastructure remains foundational.

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Register the new trusted service/VFS sources and focused hosted validation target.

- [X] T001 Add display-safe command service, VFS file source, and focused test targets to `CMakeLists.txt` and `tests/CMakeLists.txt`
- [X] T002 [P] Add feature documentation links and command-policy wording to `docs/cui.md`, `docs/inferenceos-fs.md`, and `docs/limitations.md`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Establish exact displayed-path resolution and VFS-mediated identity operations used by every story.

**⚠️ CRITICAL**: No user story mutation is safe until this phase is complete.

- [X] T003 Write failing displayed-path, intermediate-directory, truncation, malformed-entry, and kind-selection tests in `tests/integration/file_view_service_test.c`
- [X] T004 Implement complete bounded display-safe directory snapshots and exact path resolution in `src/kernel/syscall/file_view.c` and `src/kernel/include/inferenceos/file_view.h`
- [X] T005 Move runtime `dir` enumeration onto the trusted finalized snapshot and remove CUI-side label ownership in `src/kernel/runtime.c` and `src/cui/directory_commands.c`
- [X] T006 Write failing exact-object replace, append, rename, remove, stale-identity, and read-only tests in `tests/integration/file_service_test.c`
- [X] T007 Add VFS-bracketed object mutation callbacks/wrappers in `src/vfs/file.c` and `src/vfs/include/inferenceos/vfs.h`
- [X] T008 Implement identity lookup and transaction reuse in `src/filesystems/inferenceos_fs/file_service.c` and `src/filesystems/inferenceos_fs/include/inferenceos/fs/file_service.h`

**Checkpoint**: Displayed names resolve to exact objects and all mutations can flow through VFS without hidden paths.

---

## Phase 3: User Story 1 - Use Names Shown by `dir` (Priority: P1) 🎯 MVP

**Goal**: `write`, `append`, `delete`, and `fileinfo` select files using only displayed paths.

**Independent Test**: Create a typed file internally, list it, and successfully write, append, diagnose, and delete it using only the displayed name.

### Tests for User Story 1

- [X] T009 [P] [US1] Update CUI forwarding and diagnostic resolver tests to require extension-hidden affected operands in `tests/integration/fs_commands_test.c`
- [X] T010 [P] [US1] Add real-filesystem displayed-name lifecycle assertions in `tests/integration/file_service_test.c`

### Implementation for User Story 1

- [X] T011 [US1] Add trusted file-command orchestration for resolve, authorize, replace, append, remove, and diagnostic identity in `src/kernel/syscall/file_command.c` and `src/kernel/include/inferenceos/file_command.h`
- [X] T012 [US1] Wire runtime CUI callbacks and FILE-only diagnostic resolution to the trusted service in `src/kernel/runtime.c` and `src/cui/include/inferenceos/cui_fs.h`
- [X] T013 [US1] Update affected command usage strings without changing create/type/hashinfo/fatinfo behavior in `src/cui/file_commands.c` and `src/cui/diagnostic_commands.c`

**Checkpoint**: User Story 1 works independently for absolute and relative displayed paths.

---

## Phase 4: User Story 2 - Rename Without Knowing the Type (Priority: P1)

**Goal**: Rename/move by displayed source and destination base while preserving the hidden authoritative type.

**Independent Test**: Rename and move a displayed file, then verify unchanged bytes, extension, companion validity, and no-overwrite behavior.

### Tests for User Story 2

- [X] T014 [US2] Add same-directory, cross-directory, invalid-base, and existing-destination rename tests in `tests/integration/file_service_test.c`

### Implementation for User Story 2

- [X] T015 [US2] Implement destination-parent display resolution and base-only rename orchestration in `src/kernel/syscall/file_command.c`
- [X] T016 [US2] Preserve authoritative extension bytes in object rename and retain companion commit ordering in `src/filesystems/inferenceos_fs/file_service.c`

**Checkpoint**: Rename never requires or changes the hidden type.

---

## Phase 5: User Story 3 - Enforce Command/File Compatibility (Priority: P1)

**Goal**: Character writes use empty-file and complete content validation rather than a hidden-extension/type allowlist.

**Independent Test**: `write` creates a missing extensionless file or initializes an empty file regardless of type, rejects non-empty targets, and `append` accepts only empty or completely validated ASCII text while binary content remains unchanged.

### Tests for User Story 3

- [X] T017 [P] [US3] Add empty-write, complete text scan, invalid-input, binary-content, and read-only contract tests in `tests/contract/display_safe_file_command_test.c`
- [X] T018 [P] [US3] Add unexpected-format no-content/no-metadata-mutation integration cases in `tests/integration/file_service_test.c`

### Implementation for User Story 3

- [X] T019 [US3] Implement extension-independent empty-write and complete ASCII append validation in `src/kernel/syscall/file_command.c`, with identity reads through VFS and InferenceOS-FS
- [X] T019a [US4] Enforce unique extension-hidden bases across file/directory creation and rename in `src/filesystems/inferenceos_fs/file_service.c`, retaining exact labels only for legacy collision-bearing media

**Checkpoint**: Non-empty write targets and binary append targets cannot be corrupted by character-writing commands; hidden types are not authorization inputs.

---

## Phase 6: User Story 4 - Select Hidden-Name Collisions Safely (Priority: P2)

**Goal**: Exact labels such as `REPORT`, `REPORT (2)`, and `REPORT (3)` select the intended current object.

**Independent Test**: Three colliding bases map to three exact identities; missing, quoted, renumbered-current-view, and incomplete-view cases fail or resolve deterministically.

### Tests for User Story 4

- [X] T020 [US4] Add three-way collision, quoted operand, current-view renumbering, and no-guess tests in `tests/integration/file_view_service_test.c` and `tests/integration/fs_commands_test.c`

### Implementation for User Story 4

- [X] T021 [US4] Reuse one identity-ranked label algorithm for directory rendering and resolution in `src/shell/display_safe_entry.c`, `src/kernel/syscall/file_view.c`, and `src/cui/directory_commands.c`

**Checkpoint**: Every current displayed collision label selects one exact object and truncated views never resolve.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: End-to-end evidence, documentation, and compiler regressions.

- [X] T022 Update boot command audit to use displayed operands for the five affected commands in `src/kernel/runtime.c`
- [X] T023 [P] Add QEMU display-safe file-command audit and release-matrix wiring in `tests/system/display_safe_file_commands_test.ps1`, `tests/CMakeLists.txt`, and `tools/test/run_qemu_tests.ps1`
- [X] T024 [P] Update Hyper-V CUI examples in `docs/hyperv.md`
- [X] T025 Run focused unit/contract/integration suites under `gcc-host-debug` and `clang-host-debug` using `specs/004-display-safe-file-commands/quickstart.md`
- [X] T026 Build GCC and Clang freestanding images and run the dedicated QEMU audit plus existing file lifecycle/diagnostic regressions
- [X] T027 Route regular-file creation through a VFS parent-identity callback in `src/vfs/directory.c`, `src/filesystems/inferenceos_fs/file_service.c`, and `src/kernel/runtime.c`
- [X] T028 Add an eight-file relative-create regression covering current-directory listing and root isolation in `tests/integration/file_service_test.c`
- [X] T029 Extend the boot CUI audit with eight relative creates, `dir .`, and display-name deletes in `src/kernel/runtime.c`

---

## Dependencies & Execution Order

### Constitution Traceability

| Constitution gate | Implementation task(s) | Validation task(s) |
|---|---|---|
| Shared CUI/GUI namespace | T004, T011, T012 | T009, T010, T023 |
| VFS-mediated storage | T007, T008, T011 | T006, T010 |
| Authoritative type / collision safety | T004, T016, T019, T021 | T003, T014, T017, T020 |
| Durable save and companion integrity | T008, T016 | T006, T014, T018, T026 |
| Extension-hidden ordinary views | T004, T011, T013, T021 | T009, T017, T020, T023 |
| Shell/application mediation | T004, T011, T019 | T003, T017, T023 |
| Capacity and bounded execution | T004, T007, T008 | T003, T006, T020 |
| Freestanding C17 / dual compilers | T004, T007, T008, T011 | T025, T026 |

### Phase Dependencies

- Phase 1 has no dependencies.
- Phase 2 depends on T001 and blocks all stories.
- US1 depends on Phase 2 and provides the MVP command path.
- US2 and US3 depend on Phase 2 plus the shared command orchestration from T011.
- US4 depends on the Phase 2 resolver and can proceed independently of rename/type policy.
- Polish depends on all selected stories.

### User Story Dependencies

- **US1**: Phase 2 only.
- **US2**: Phase 2 and T011; independently validated through rename behavior.
- **US3**: Phase 2 and T011; independently validated through compatibility decisions.
- **US4**: Phase 2; independently validated through resolver/label behavior.

### Parallel Opportunities

- T002 can run alongside T001.
- Test design T003 and T006 touches separate test files and can run in parallel.
- US1 CUI tests T009 and filesystem tests T010 can run in parallel.
- US3 contract and mutation-preservation tests T017 and T018 can run in parallel.
- QEMU automation T023 and Hyper-V documentation T024 can run in parallel after behavior stabilizes.

---

## Parallel Example: User Story 3

```text
Task: "Add empty-write and complete content-validation contract tests in tests/contract/display_safe_file_command_test.c"
Task: "Add unexpected-format no-content/no-metadata-mutation integration cases in tests/integration/file_service_test.c"
```

---

## Implementation Strategy

### MVP First

1. Complete setup and foundational resolver/VFS identity work.
2. Complete US1 so displayed names drive write, append, fileinfo, and delete.
3. Run the independent lifecycle test before proceeding.

### Incremental Delivery

1. Foundation → exact selected object and VFS mutations.
2. US1 → basic affected commands.
3. US2 → base-only type-preserving rename.
4. US3 → safe text compatibility policy.
5. US4 → complete collision behavior.
6. Polish → system evidence and dual-toolchain validation.

## Notes

- Every task uses the required checkbox, sequential ID, optional parallel marker, story label where required, and exact file paths.
- Existing internal canonical-name tests remain as low-level filesystem regressions; ordinary affected CUI operands change to displayed names.
- `type` remains outside this feature; `create`, `hashinfo`, and `fatinfo` are covered only where required to prevent regressions from the shared display-safe/VFS changes.
