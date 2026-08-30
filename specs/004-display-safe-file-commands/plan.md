# Implementation Plan: Display-Safe File Commands

**Branch**: `004-display-safe-file-commands` | **Date**: 2026-08-29 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/004-display-safe-file-commands/spec.md`

## Summary

Make `write`, `append`, `rename`, `delete`, and `fileinfo` consume the extension-hidden paths shown by `dir`. A trusted kernel file-view resolver walks display-safe components, reproduces stable legacy collision labels, authorizes the requested operation, and returns an internal object identity. VFS and InferenceOS-FS provide identity-based reads and mutations so the CUI never reconstructs hidden extensions or bypasses VFS. Rename accepts a new visible base name and preserves the selected object's authoritative extension. New creation and rename enforce one visible base per directory. Character-write compatibility is based on emptiness and complete text-byte validation, never an embedded type allowlist.

## Requirements Traceability

| Spec reference | Design responsibility | Contract / artifact | Validation approach |
|---|---|---|---|
| US1, FR-001–FR-006 | Resolve displayed paths to exact permitted objects | [display-file-operations.md](contracts/display-file-operations.md) | File-view resolver integration tests and CUI command tests |
| US2, FR-009–FR-010 | Rename by identity while preserving hidden type | [data-model.md](data-model.md), command contract | InferenceOS-FS identity mutation and rename persistence tests |
| US3, FR-007–FR-008 | Extension-independent text-content policy | [research.md](research.md), command contract | Empty write, complete ASCII append scan, binary denial, and unexpected-format tests |
| US4, FR-003–FR-005 | Collision-safe label selection | Displayed-path resolution contract | `REPORT`, `REPORT (2)`, and `REPORT (3)` identity-selection tests |
| FR-011–FR-014 | Exact deletion, privileged diagnostics, and metadata boundaries | Kernel/VFS/filesystem boundary contract | Delete/fileinfo integration tests and output assertions |
| FR-015–FR-017 | No format change, bounded execution, dual compilers | [quickstart.md](quickstart.md) | GCC and Clang hosted suites plus existing lifecycle regressions |

## Technical Context

**Language/Version**: Freestanding ISO C17 for production code; PowerShell/CMake test orchestration

**Primary Dependencies**: Existing CUI registry, kernel file-view service, display-safe entry model, VFS enumeration and operation flags, InferenceOS-FS file service, diagnostic capability service

**Storage**: Existing version-1 InferenceOS-FS primary/companion records and FAT-derived allocation; no on-disk change

**Testing**: Hosted unit, contract, and integration tests with warnings as errors; existing QEMU CUI command audit for end-to-end confirmation

**Target Platform**: x86-64 UEFI operating system, QEMU q35/TCG reference environment; hosted GCC and Clang test configurations

**Project Type**: Bootable operating-system demonstrator with kernel, VFS, filesystem, shell, CUI, and GUI layers

**Performance Goals**: Resolve any selectable name within the existing 64-entry CUI directory bound without dynamic allocation; fail deterministically when the complete view exceeds that bound

**Constraints**: Hidden extensions/hashes never cross the ordinary CUI boundary; exact collision-safe selection; VFS-mediated storage; identity-based mutations; plain-text-only character writes; bounded freestanding data structures; durable companion ordering

**Scale/Scope**: Five CUI commands, 255-byte paths, 16 path levels, 64 selectable directory entries, 1 MiB current file-operation limit, and volumes of at least 50,000,000,000 bytes

## Constitution Check

*GATE: Must pass before Phase 0 research and again after Phase 1 design.*

### Pre-Research Gate

- **Demonstrable OS scope / shared CUI and GUI**: **PASS** — The feature repairs the existing shared file lifecycle and changes no GUI or recovery boundary.
- **VFS-mediated storage**: **PASS** — Resolution uses kernel file-view/VFS enumeration and mutations remain in InferenceOS-FS behind VFS-owned identities.
- **Authoritative extension / collision safety**: **PASS** — The authoritative type stays internal and visible collision labels resolve to exact object identities.
- **Durable save and filesystem integrity**: **PASS** — Identity APIs reuse existing save, rename, delete, and companion transaction primitives.
- **Extension-hidden views**: **PASS** — Ordinary operands contain only displayed paths; diagnostics remain explicitly privileged.
- **Application and shell mediation**: **PASS** — Compatibility and selection live in trusted kernel services, not CUI parsing.
- **Research-gated registry**: **PASS** — The design does not consult or change the registry.
- **Capacity / GUI / build constraints**: **PASS** — Bounds, GUI layering, freestanding C17, and dual compilers are retained.

### Post-Design Gate

- **All applicable gates**: **PASS** — [research.md](research.md), [data-model.md](data-model.md), and [display-file-operations.md](contracts/display-file-operations.md) define exact selection, authorization, identity mutation, preservation, failure, and validation behavior without an on-disk or dependency-direction exception.

## Project Structure

### Documentation (this feature)

```text
specs/004-display-safe-file-commands/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   `-- display-file-operations.md
`-- tasks.md
```

### Source Code (repository root)

```text
src/
|-- kernel/
|   |-- syscall/file_view.c
|   |-- include/inferenceos/file_view.h
|   `-- runtime.c
|-- vfs/
|   |-- file.c
|   `-- include/inferenceos/vfs.h
|-- filesystems/inferenceos_fs/
|   |-- file_service.c
|   `-- include/inferenceos/fs/file_service.h
|-- cui/
|   |-- file_commands.c
|   `-- diagnostic_commands.c
`-- shell/display_safe_entry.c

tests/
|-- integration/file_view_service_test.c
|-- integration/file_service_test.c
|-- integration/fs_commands_test.c
`-- system/ and runtime command-audit regressions
```

**Structure Decision**: Extend the kernel file-view service for display-safe selection, add VFS-bracketed exact-object mutations, and implement those callbacks in the existing InferenceOS-FS file service. The CUI remains a thin dispatcher. No filesystem-specific dependency is introduced into CUI, Shell, or the generic VFS API.

## Phase 0 Research Decisions

- **Decision**: Resolve every path component against the bounded display-safe directory model and return an internal object identity plus trusted type and operation metadata.
  - **Rationale**: The exact labels already define the ordinary namespace and stable collision behavior.
  - **Alternatives considered**: Guessing an extension, accepting canonical paths, or resolving inside CUI would violate metadata hiding and could select the wrong object.
- **Decision**: Add VFS-bracketed identity-based read, replace, append, rename, and remove entry points, backed by InferenceOS-FS callbacks that reuse existing read and transaction primitives.
  - **Rationale**: Operations and compatibility checks remain exact after selection, never need a reconstructed extension-bearing path, and retain the mandatory VFS storage boundary.
  - **Alternatives considered**: Rebuilding canonical paths in runtime duplicates filesystem naming policy and makes directory moves fragile.
- **Decision**: Preserve the source extension during rename and accept only a new extension-free base leaf.
  - **Rationale**: Rename remains usable in the hidden view without becoming an implicit type conversion.
  - **Alternatives considered**: Exposing destination extensions breaks the ordinary contract; inferring a new type from content is unreliable.
- **Decision**: Require existing `write` targets to be empty, create a missing write target without an extension, and require `append` to validate all existing bytes as printable ASCII, tab, carriage return, or line feed. Do not consult type identity.
  - **Rationale**: Empty content can be initialized safely, while complete validation fails before mutation for existing binary/image content regardless of extension.
  - **Alternatives considered**: Extension/icon allowlists misclassify content; prefix-only sniffing can miss later binary bytes; blind replacement can destroy non-text files.
- **Decision**: Fail with a bounded error if a directory cannot be represented completely by the existing 64-entry CUI view.
  - **Rationale**: Partial views can change collision ranks and therefore cannot be resolved safely.
  - **Alternatives considered**: Selecting from a truncated page risks targeting a different object than the user saw.
- **Decision**: Treat collision labels as stable within the current complete listing and bind each synchronous command to the resolved object identity until mutation completes.
  - **Rationale**: This prevents in-command retargeting while accurately reflecting the existing set-relative ranking scheme.
  - **Alternatives considered**: Persisting stale labels across namespace changes would require per-console snapshot generations and mandatory relisting, which is outside this bug fix.
- **Decision**: Reject file/directory creation and rename when any destination entry has the same canonical eight-byte base, regardless of extension.
  - **Rationale**: The extension-hidden base is the ordinary namespace and must be unique for newly produced state.
  - **Alternatives considered**: Automatic deletion or renaming is destructive; rejecting legacy media prevents repair.

## Phase 1 Design Outputs

- **Data model**: Displayed operands, resolved objects, compatibility decisions, and rename transitions are defined in [data-model.md](data-model.md).
- **Contracts**: Command syntax, trusted resolution, authorization, failure atomicity, and metadata boundaries are defined in [display-file-operations.md](contracts/display-file-operations.md).
- **Quickstart validation**: End-to-end text, binary-denial, collision, rename, fileinfo, and delete scenarios are in [quickstart.md](quickstart.md).
- **Post-design constitution result**: **PASS** — no exception or amendment is required.

## Complexity Tracking

No constitutional violations or exceptions are required.
