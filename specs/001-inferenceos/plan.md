# Implementation Plan: InferenceOS CUI/GUI Filesystem Demonstrator

**Branch**: `001-inferenceos` | **Date**: 2026-08-23 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-inferenceos/spec.md`

## Summary

Build a bootable x86-64 UEFI operating-system demonstrator with independent CUI recovery, a
layered GUI, one VFS namespace, and a persistent 50 GB-or-larger InferenceOS-FS volume. A native
UEFI loader hands an ELF64 kernel a GOP framebuffer and memory map. Ring-3 Shell/UI applications use
versioned syscalls, IPC, opaque handles, and display-safe metadata. Virtio-blk, an ordered block
cache, and companion commit fencing implement durable collision-safe filesystem behavior.

## Requirements Traceability

| Spec reference | Design responsibility | Contract / artifact | Validation approach |
|---|---|---|---|
| US1, FR-001–022 and FR-024–026 | boot, kernel, CUI, GUI | `contracts/boot-and-runtime.md` | repeated boot, GUI start/failure |
| US2/11, FR-023 | GUI-terminal and standalone-CUI mounted-namespace coherence | `contracts/boot-and-runtime.md`, `contracts/shell-application.md` | post-mount GUI-terminal/CUI read-write visibility test |
| US1/5, FR-027–040 | scheduler, syscall, IPC, Shell | `contracts/boot-and-runtime.md`, `contracts/shell-application.md` | pointer, lifecycle, IPC, and recovery tests |
| US2, FR-054–088 | block, formatter, mount | `data-model.md`, filesystem contract | 50 GB geometry and corrupt mount tests |
| US3/11, FR-089–189 | InferenceOS-FS mutation | `contracts/inferenceos-fs.md` | fault injection and reboot cycles |
| US4/5, FR-190–209 | Shell and File Explorer | `contracts/shell-application.md` | DTO leak and shared namespace tests |
| US6, FR-138–151, FR-204–205, FR-237, FR-252–253 | extension registry, enabled type views, diagnostics, and benchmark gate | `research.md` R12 | lifecycle, reboot fallback, Shell-backed type-view, diagnostic, and matched enabled/disabled benchmark tests |
| US7/8, FR-210–223 | application routing | `contracts/shell-application.md` | identity, forged-handle, denied-query tests |
| US1/4, FR-224–232 | graphics, input, windowing | `contracts/boot-and-runtime.md` | renderer, input, composition, and failure tests |
| US9, FR-233–236 and FR-238 | authoritative filesystem diagnostics | runtime/application contracts | bounded explicit diagnostic tests |
| US10, FR-041–053/152–168 | VFS/directories | filesystem contract | lifecycle and path-boundary tests |
| US12, FR-005–015/239–260 | build and quality | `quickstart.md` | GCC/Clang and QEMU validation matrix |

## Technical Context

**Language/Version**: Freestanding ISO C17; narrowly allowlisted GCC/Clang extensions; isolated
x86-64 assembly for entry, traps, context transition, and CPU control only

**Primary Dependencies**: GCC cross-toolchain/binutils, Clang/LLD, QEMU q35, OVMF, CMake plus Ninja,
project-owned UEFI definitions/loader and SHA-256 implementation, licensed PSF2 font; no hosted runtime

**Storage**: Separate FAT ESP and sparse 64 GiB raw InferenceOS-FS disk; virtio-blk; 512-byte sector
write-back cache with ordered flush generations

**Testing**: Host unit/property tests, hosted block/VFS integration tests, QEMU serial/framebuffer
system tests, deterministic corruption and fault injection, GCC/Clang matrix

**Target Platform**: x86-64 UEFI, QEMU q35 + OVMF + TCG baseline; standard VGA GOP at 1024x768
BGRX8888; PS/2 keyboard and mouse; virtio-blk persistent disk

**Project Type**: Greenfield modular-monolithic operating-system demonstrator with separate,
statically linked ELF64 ring-3 system-application processes

**Performance Goals**: Meet SC-001–020; dirty-region GUI repaint; registry gate of at least 10%
median instruction or end-to-end latency improvement and at most 5% durable-save regression

**Constraints**: One shared VFS; fixed version-1 on-disk layouts; authoritative extension and
collision verification; hidden ordinary metadata; CUI recovery; no journal/automatic repair;
all project source beneath `src`

**Scale/Scope**: One x86-64 VM, one persistent root volume >=50,000,000,000 bytes, FAT32-derived
8.3 names and 4 GiB-minus-one maximum file size, one desktop, one CPU, and a preemptive round-robin
scheduler for minimal static processes

## Constitution Check

*GATE: Must pass before Phase 0 research and again after Phase 1 design.*

### Pre-Research Gate

- **CUI/GUI and recovery**: PASS — both are planned over shared services; GUI failure returns input
  and control to CUI.
- **VFS/storage layering**: PASS — applications → syscall/IPC/Shell → VFS → InferenceOS-FS → block
  cache → virtio-blk.
- **Primary/companion integrity**: PASS — distinct 32-byte records and uncommitted fencing preserve
  authoritative-extension and collision rules.
- **Metadata hiding/application mediation**: PASS — process-local capabilities and safe DTOs omit
  extension/hash; diagnostics use separate authority.
- **Durability/capacity**: PASS — ordered barriers and a 64 GiB reference volume cover the 50 GB
  minimum and failure semantics.
- **Registry**: PASS — derived, rebuildable, default-off, and measured against a fixed gate.
- **GUI/input layering**: PASS — GOP device boundary, renderer, retained window manager, widgets,
  and shared normalized input are distinct.
- **C17/reproducibility**: PASS — dual cross-compiler profiles, allowlist, fixed-layout assertions,
  isolated assembly, and headless QEMU workflow are explicit.
- **Release claims**: PASS — no production security, networking, POSIX, dynamic linking, journaling,
  or unmeasured registry benefit is claimed.

### Post-Design Gate

- **All gates**: PASS — `research.md`, `data-model.md`, contracts, and `quickstart.md` preserve every
  pre-research gate. No design introduces a VFS bypass, GUI/filesystem coupling, metadata leak,
  collision-based identity, unsafe mount, unflushed durable success, or registry authority.

## Project Structure

### Documentation (this feature)

```text
specs/001-inferenceos/
|-- spec.md
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- boot-and-runtime.md
|   |-- inferenceos-fs.md
|   `-- shell-application.md
`-- tasks.md                 # generated by $speckit-tasks
```

### Source Code (repository root)

```text
src/
|-- boot/uefi/
|-- boot/system_modules/
|-- arch/x86_64/
|-- kernel/{memory,interrupts,process,scheduler,syscall,ipc}/
|-- runtime/
|-- drivers/{serial,framebuffer,ps2,virtio_blk}/
|-- block/
|-- vfs/
|-- filesystems/inferenceos_fs/
|-- cui/
|-- gui/{graphics,input,window,widgets,desktop,terminal,file_explorer}/
|-- shell/
`-- applications/{proprietary_test,custom_test}/

tests/
|-- unit/
|-- integration/
|-- system/
|-- fault/
`-- benchmarks/

tools/
|-- image/
`-- test/
```

**Structure Decision**: A single project-owned `src` tree follows constitutional dependency
direction. Pure algorithms remain host-testable; tests/tools/build outputs are outside `src` as
explicitly permitted. Filesystem code has no GUI imports, and UI/application code has no raw
filesystem or block imports.

## Phase 0 Research Decisions

Research resolved boot, toolchain, platform, graphics/input/windowing, block/cache, filesystem
ordering, mount states, syscall/IPC, safe metadata, CUI grammar, scheduling, initial ring-3 module
packaging, validation, and registry benchmark choices. Full decisions and rejected alternatives are
in [research.md](research.md). No unresolved clarifications remain.

## Phase 1 Design Outputs

- **Data model**: [data-model.md](data-model.md) defines persistent structures, runtime objects,
  validation rules, relationships, and commit/mount transitions.
- **Contracts**: [contracts](contracts/) defines boot/runtime, filesystem, and Shell/application
  boundaries including failure semantics and forbidden data.
- **Quickstart validation**: [quickstart.md](quickstart.md) gives a clean build and end-to-end proof
  sequence without implementation bodies.
- **Post-design constitution result**: PASS; no corrective amendment or complexity exception needed.

## Complexity Tracking

No constitutional violations require justification.
