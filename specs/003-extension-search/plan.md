# Implementation Plan: Extension Search

**Branch**: `003-extension-search` | **Date**: 2026-08-28 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/003-extension-search/spec.md`

## Summary

Add `search <extension>` to the CUI. The command submits one bounded request to a kernel-owned shell-facing search service. That service calls a new VFS search operation; InferenceOS-FS canonicalizes the extension, computes FNV-1a-32 internally, traverses directory entries, uses the companion hash as a prefilter, verifies every healthy primary/companion pair and exact authoritative extension, and returns only extension-hidden absolute locations. The CUI renders the returned array without learning or displaying a hash.

## Requirements Traceability

| Spec reference | Design responsibility | Contract / artifact | Validation approach |
|---|---|---|---|
| US1, FR-001–FR-007 | CUI command, syscall service, VFS operation, filesystem search | `contracts/search-command.md`, `contracts/search-service.md` | CUI and filesystem integration tests |
| US2, FR-008–FR-010 | Bounded display-safe reply and renderer | `data-model.md`, `contracts/search-service.md` | Contract test scans replies/output for forbidden metadata |
| US3, FR-011–FR-015 | Pair validation, hash prefilter plus exact comparison, registry-independent traversal | `research.md`, `contracts/vfs-extension-search.md` | Collision candidate, corruption, bounds, and dual-toolchain tests |

## Technical Context

**Language/Version**: Freestanding ISO C17; PowerShell for system validation

**Primary Dependencies**: Existing CUI registry, kernel shell-facing syscall services, VFS mount contract, InferenceOS-FS file service and FNV-1a-32 companion implementation

**Storage**: Existing versioned InferenceOS-FS primary and companion records; no on-disk format change

**Testing**: Hosted unit, contract, and integration tests; QEMU CUI audit; GCC and Clang preset builds

**Target Platform**: x86-64 UEFI; QEMU reference platform and qualified Hyper-V Generation 2 path

**Project Type**: Freestanding operating system

**Performance Goals**: One bounded search completes without heap allocation; result storage is fixed at 16 paths; traversal terminates at the VFS depth and path limits; an over-capacity match is detected and reported explicitly

**Constraints**: Hashes and extensions remain hidden from ordinary replies; exact extension verification is mandatory; VFS mediation is mandatory; no registry dependency; read-only operation; fixed-capacity and overflow-safe code

**Scale/Scope**: Mounted root namespace, up to 16 directory levels, 255-byte paths, 16 returned locations per command, existing 64 GiB reference volume and 50,000,000,000-byte minimum capacity

## Constitution Check

*GATE: Must pass before Phase 0 research and again after Phase 1 design.*

### Pre-Research Gate

- **CUI/GUI and recovery**: **PASS** — Adds a CUI command without changing GUI startup or recovery behavior.
- **VFS boundary**: **PASS** — The syscall service invokes a VFS operation; shell/CUI code cannot read raw records.
- **Companion and collision safety**: **PASS** — Filesystem filtering requires healthy pair validation, hash equality, and exact extension equality.
- **Hidden metadata and shell mediation**: **PASS** — Request contains user text; reply contains only display-safe paths, count, and truncation.
- **Registry independence**: **PASS** — Authoritative directory traversal is the required baseline.
- **Durability and on-disk format**: **PASS** — Search performs no writes and introduces no persisted structures.
- **Capacity, layering, and toolchains**: **PASS** — Fixed bounds, existing dependency direction, freestanding C17, and GCC/Clang validation remain required.
- **Release claims**: **PASS** — Claims remain limited to tested extension search behavior.

### Post-Design Gate

- **CUI/GUI and recovery**: **PASS** — `search-command.md` adds only a command handler and output contract.
- **VFS boundary**: **PASS** — `vfs-extension-search.md` makes the mount callback private to VFS and filesystem layers.
- **Companion and collision safety**: **PASS** — `research.md` selects hash-prefilter plus exact canonical comparison after record-pair validation.
- **Hidden metadata and shell mediation**: **PASS** — `search-service.md` excludes hashes, extensions, and raw records from its reply type.
- **Registry independence**: **PASS** — No registry API appears in the design contract.
- **Durability and on-disk format**: **PASS** — `data-model.md` defines transient request/reply entities only.
- **Capacity, layering, and toolchains**: **PASS** — Contracts specify 16 results, 255-byte locations, 16 traversal levels, C17, and dual builds.
- **Release claims**: **PASS** — `quickstart.md` gives the exact proof required before claiming support.

## Project Structure

### Documentation (this feature)

```text
specs/003-extension-search/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- search-command.md
|   |-- search-service.md
|   `-- vfs-extension-search.md
`-- tasks.md
```

### Source Code (repository root)

```text
src/
|-- kernel/
|   |-- include/inferenceos/extension_search.h
|   `-- syscall/extension_search.c
|-- vfs/
|   |-- include/inferenceos/vfs.h
|   `-- directory.c
|-- filesystems/inferenceos_fs/
|   |-- include/inferenceos/fs/file_service.h
|   |-- records.c
|   `-- file_service.c
|-- cui/
|   |-- include/inferenceos/cui_fs.h
|   |-- file_commands.c
|   `-- fs_commands.c
`-- kernel/runtime.c

tests/
|-- unit/directory_record_test.c
|-- contract/extension_search_service_test.c
|-- integration/file_service_test.c
|-- integration/fs_commands_test.c
`-- system/qemu_profile_test.ps1
```

**Structure Decision**: Extend the existing single operating-system source tree. CUI depends on a narrow callback, runtime adapts it to the kernel search service, the kernel depends only on VFS result types, VFS delegates to its mounted filesystem, and InferenceOS-FS alone sees extension hashes and authoritative extensions.

## Phase 0 Research Decisions

- **Decision**: Use one extension-search syscall contract instead of returning a hash to shell code.
  - **Rationale**: It meets the requested user flow while satisfying constitutional metadata hiding and shell mediation.
  - **Alternatives considered**: A public hash syscall was rejected because it exposes hidden derived metadata; direct CUI filesystem access was rejected because it bypasses mediation.
- **Decision**: Perform authoritative recursive traversal without the registry.
  - **Rationale**: It remains correct with the registry disabled, missing, stale, corrupt, or full.
  - **Alternatives considered**: Registry-only lookup was rejected as non-authoritative and not enabled by default.
- **Decision**: Match with validated companion hash prefilter followed by exact canonical extension comparison.
  - **Rationale**: This preserves the filesystem experiment and makes collisions harmless.
  - **Alternatives considered**: Hash-only matching was rejected as incorrect; extension-only matching would fail to exercise the companion prefilter requirement.
- **Decision**: Return at most 16 fixed-capacity absolute display-safe locations and an explicit truncation flag.
  - **Rationale**: This bounds kernel stack/storage use and makes incomplete output visible.
  - **Alternatives considered**: Unbounded allocation is unavailable; persisted search cursors add unnecessary state for the demonstrator.

## Phase 1 Design Outputs

- **Data model**: Transient request, canonical query, match candidate, result location, and bounded reply are defined in `data-model.md`.
- **Contracts**: User command, shell-facing service, and VFS/filesystem callback contracts are defined under `contracts/`.
- **Quickstart validation**: `quickstart.md` proves nested matching, canonicalization, no-match output, invalid input, metadata hiding, and truncation.
- **Post-design constitution result**: **PASS** — no exception or amendment is required.

## Complexity Tracking

No constitutional violations or exceptions.
