# Tasks: Extension Search

**Input**: Design documents from `/specs/003-extension-search/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: Unit, contract, integration, CUI system, and dual-toolchain validation are mandatory.

## Phase 1: Setup (Shared Infrastructure)

- [X] T001 Add feature sources and tests to `CMakeLists.txt` and `tests/CMakeLists.txt`

---

## Phase 2: Foundational (Blocking Prerequisites)

- [X] T002 Define bounded VFS search result and mount callback contracts in `src/vfs/include/inferenceos/vfs.h`
- [X] T003 Define the metadata-safe request/reply service contract in `src/kernel/include/inferenceos/extension_search.h`

---

## Phase 3: User Story 1 - Find Files by Extension (Priority: P1) MVP

**Goal**: `search <extension>` returns matching display-safe locations from the root and nested directories.

**Independent Test**: Create DOC and TXT files across nested directories and verify only DOC locations are rendered.

- [X] T004 [P] [US1] Add extension query canonicalization tests in `tests/unit/directory_record_test.c`
- [X] T005 [P] [US1] Add recursive filesystem search tests in `tests/integration/file_service_test.c`
- [X] T006 [US1] Implement extension query canonicalization in `src/filesystems/inferenceos_fs/records.c` and `src/filesystems/inferenceos_fs/include/inferenceos/fs/records.h`
- [X] T007 [US1] Implement recursive hash-prefiltered exact search in `src/filesystems/inferenceos_fs/file_service.c` and `src/filesystems/inferenceos_fs/include/inferenceos/fs/file_service.h`
- [X] T008 [US1] Implement VFS search delegation and result validation in `src/vfs/directory.c`
- [X] T009 [US1] Implement kernel search dispatch in `src/kernel/syscall/extension_search.c`
- [X] T010 [US1] Register and wire `search <extension>` through `src/cui/file_commands.c`, `src/cui/fs_commands.c`, `src/cui/include/inferenceos/cui_fs.h`, and `src/kernel/runtime.c`

---

## Phase 4: User Story 2 - Preserve Hidden Metadata (Priority: P2)

**Goal**: Replies and CUI output contain only extension-hidden paths and status metadata.

**Independent Test**: Verify returned structures and captured CUI output contain neither `.DOC` nor hash fields/text.

- [X] T011 [P] [US2] Add syscall boundary and forbidden-metadata contract tests in `tests/contract/extension_search_service_test.c`
- [X] T012 [US2] Add CUI rendering, no-match, usage, and truncation tests in `tests/integration/fs_commands_test.c`

---

## Phase 5: User Story 3 - Remain Correct Under Collisions and Damage (Priority: P3)

**Goal**: Hash-prefilter candidates are returned only after healthy pair and exact extension verification, without registry dependence.

**Independent Test**: Exercise mismatched hash/extension metadata, invalid pairs, bounds, and registry-disabled operation.

- [X] T013 [US3] Add collision-safety, malformed-pair, depth/path, and over-capacity tests in `tests/integration/file_service_test.c` and `tests/contract/extension_search_service_test.c`

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T014 [P] Document command behavior and metadata guarantees in `docs/cui.md` and `docs/inferenceos-fs.md`
- [X] T015 Run hosted GCC and Clang configure/build/test validation and update `specs/003-extension-search/quickstart.md` if commands differ
- [X] T016 Run the applicable QEMU CUI audit or record the exact environmental blocker in `specs/003-extension-search/quickstart.md`

---

## Dependencies & Execution Order

### Constitution Traceability

| Constitution gate | Implementation task(s) | Validation task(s) |
|---|---|---|
| VFS mediation | T002, T008, T009, T010 | T011, T012 |
| Hash is prefilter; exact extension authoritative | T006, T007 | T005, T013 |
| Ordinary views hide extensions and hashes | T003, T009, T010 | T011, T012 |
| Registry-disabled correctness | T007 | T013 |
| Fixed bounds and freestanding C17 | T002, T003, T007–T010 | T004, T005, T011, T013, T015 |

### Phase Dependencies

- Phase 1 precedes compilation of new files.
- Phase 2 blocks all user-story implementation.
- US1 establishes the end-to-end path and blocks US2 output tests and US3 adversarial tests.
- Documentation can proceed after contracts stabilize; final validation follows all implementation.

### Parallel Opportunities

- T004 and T005 can be written independently before implementation.
- T011 can be prepared independently from T012 once service structures are defined.
- T014 can proceed while hosted validation runs.

## Implementation Strategy

Implement the VFS and syscall contracts first, add failing tests, then complete the filesystem matcher and CUI wiring. Validate the MVP nested search before adversarial and metadata-boundary cases, then run both compiler suites and the boot-level audit.
