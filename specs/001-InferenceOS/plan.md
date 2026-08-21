# Implementation Plan: InferenceOS Minimal InferenceFS-FAT32 Demonstrator

**Branch**: `001-InferenceOS` | **Date**: 2026-08-21 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/001-InferenceOS/spec.md`

## Summary

Build a reproducible, single-CPU x86-64 UEFI kernel that boots under QEMU to a character prompt and demonstrates persistent file operations through a strict VFS boundary. The 64 MiB reference disk uses InferenceFS-FAT32 v1: one FAT, 4 KiB clusters, 8.3 names, and a distinct 32-byte FNV-1a extension-hash companion immediately preceding every 32-byte regular-file record. A generic block API, polling ATA PIO driver, bounded sector cache, ordered metadata updates, diagnostics, host unit tests, and QEMU fault tests provide safety and observability.

## Technical Context

**Language/Version**: Freestanding ISO C17; isolated x86-64 assembly for boot entry, UEFI ABI trampoline, interrupt stubs, and unavoidable CPU operations; Python 3.12+ only for host automation

**Primary Dependencies**: GCC 16.2 `x86_64-elf` primary toolchain, Clang/LLVM 22.1.8 validation toolchain, GNU binutils 2.45, QEMU 11.1.x, OVMF edk2-stable202605, CMake 4.1+, Ninja 1.12+; no hosted runtime in the kernel

**Storage**: Separate UEFI FAT32 boot image and raw whole-disk InferenceFS-FAT32 image; 512-byte sectors, 4 KiB clusters, one FAT; 16 MiB–1 GiB supported, 64 MiB reference

**Testing**: Host-native C/CTest unit tests under GCC and Clang; headless serial/QMP QEMU integration tests; deterministic disk mutation and block fault injection; golden hash/CRC vectors

**Target Platform**: One-vCPU x86-64 QEMU PC/i440fx, OVMF UEFI, PIIX IDE, polling ATA PIO

**Project Type**: Freestanding operating-system/filesystem demonstrator with host build and validation tools

**Performance Goals**: Non-gating engineering targets are to reach `InferenceOS>` within 5 seconds and acknowledge interactive commands within 1 second except format/sync; SC-001 remains the authoritative boot acceptance criterion. The binding safety goal is to bound every scan by the geometry of a volume no larger than 1 GiB.

**Constraints**: No host libc in kernel; no scheduler, user mode, network, graphics, partition parser, journaling, or automatic repair; checked disk arithmetic; 255-byte paths, 16 directory levels; acknowledged durable operations survive orderly reboot

**Scale/Scope**: One mount at `/`, one ATA data disk, 28 commands, 8.3 names, files up to `0xFFFFFFFF` bytes subject to capacity

## Constitution Check

*GATE: PASS before Phase 0 research; PASS after Phase 1 design.*

| Constitutional gate | Result and design evidence |
|---|---|
| Minimal filesystem demonstration | PASS — layout contains only boot/kernel support, prompt, VFS, one filesystem, one block path, and tests. |
| Character prompt is primary | PASS — `contracts/commands.md` defines required prompt commands. |
| Mandatory VFS boundary | PASS — `contracts/vfs.md` uses opaque handles; raw inspection is a separate read-only diagnostic contract. |
| InferenceFS-FAT32 behind VFS | PASS — its adapter consumes the generic block contract and implements VFS operations. |
| Distinct companion and primary records | PASS — `data-model.md` preserves two adjacent independent 32-byte records. |
| Hash remains derived metadata | PASS — lookup verifies canonical extension and full primary name after hash prefiltering. |
| Persistent entry-set integrity | PASS — ordered create, rename, delete, and flush transitions are defined below. |
| Observable experiment | PASS — `fsinfo`, `fileinfo`, `hashinfo`, and `fatinfo` contracts expose validated data. |
| Simplicity and C17 reproducibility | PASS — synchronous single-context design, pinned tools, dual compilers, static layout assertions. |
| Deferred features stay deferred | PASS — no process model, networking, graphics, or security domains are introduced. |

No constitutional violation needs an exception.

## Architecture and Requirement Mapping

| Readiness mapping | Owning components | Contract/artifact | Validation |
|---|---|---|---|
| 1. Boot/kernel initialization | `boot/uefi`, `kernel/init`, `arch/x86_64` | Versioned UEFI handoff | Serial milestones and 20-boot smoke test |
| 2. Console/keyboard | `drivers/console`, `drivers/input`, `shell` | Character source/sink | Editing, bounds, malformed input |
| 3. Block device/ATA PIO | `block/device`, `drivers/storage/ata_pio` | `contracts/block-device.md` | Fake-device and QEMU I/O tests |
| 4. Block cache | `block/cache` | Sector acquire/dirty/flush API | Eviction/order/failure tests |
| 5. VFS | `vfs` | `contracts/vfs.md` | Fake-FS and path contract tests |
| 6. Format/mount | `fs/inferencefs/format.c`, `mount.c` | Volume model | Golden and corrupt images |
| 7. FAT allocation/traversal | `fs/inferencefs/fat.c` | Bounded iterator/allocator | Loop/range/full/fragmentation tests |
| 8. Directory records | `fs/inferencefs/directory.c` | Typed slot parser | Pair/corruption/interruption tests |
| 9. FNV-1a hashing | `lib/fnv1a.c`, filename layer | Canonical extension → hash | Golden vectors and collision seam |
| 10. CRC-32 | `lib/crc32.c`, validators | CRC-32/ISO-HDLC | Vectors and bit corruption |
| 11. File/directory operations | InferenceFS adapter and VFS | Opaque synchronous calls | Host model and QEMU lifecycle |
| 12. Commands | `shell/commands` | `contracts/commands.md` | Serial transcripts |
| 13. Host unit tests | `tests/unit`, `tests/support` | Native pure-C modules | CTest under both compilers |
| 14. QEMU integration | `tests/integration`, `tools` | Serial/QMP harness | E2E, persistence, corruption, faults |
| 15. GCC/Clang C17 profiles | `cmake/toolchains` | Shared ABI/warning policy | Dual clean builds/static assertions |

## Design and Sequencing

1. Establish pinned build profiles, reviewed fixed-address ELF linker layout, boot image packaging, serial/panic output, and boot-to-prompt.
2. Add only the memory/page/heap support needed for bounded kernel objects, cache entries, VFS handles, and command buffers.
3. Implement console/input and block contracts; prove ATA I/O before filesystem work.
4. Implement endian helpers, checked arithmetic, CRC, FNV-1a, short-name parsing, and layout assertions as host-tested pure modules.
5. Implement formatter and read-only mount validation before mutation; geometry validation precedes any derived disk access.
6. Implement bounded FAT and typed directory parsing, then expose the filesystem through VFS.
7. Add create/read, write/append, directories, rename, delete, and flush/unmount in that order, with fault tests for every mutation boundary.
8. Add thin shell handlers over VFS and a separate filesystem diagnostic interface.
9. Gate release on the mandatory demonstration, 20-cycle persistence, dual compilers, corruption/fault suites, and independently usable format docs.

### Persistence ordering

- Create: persist uncommitted companion and following primary; flush; set committed and recompute CRC; flush; only then report durable success.
- Extend/write: zero/persist new data, persist new FAT entries, link the chain, then publish first cluster/size.
- Rename: clear committed and flush, update primary and flush, then publish the recomputed committed companion. Interrupted sets remain detectably incomplete.
- Delete: hide/flush the pair, mark both slots deleted/flush, then free the previously validated owned chain.
- `sync`, unmount, reboot, and shutdown perform filesystem ordering, dirty-cache writeback, then device flush. Failure propagates without a durability claim.

## Project Structure

### Documentation

```text
specs/001-InferenceOS/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- block-device.md
|   |-- commands.md
|   `-- vfs.md
`-- tasks.md                 # generated later by $speckit-tasks
```

### Source Code

```text
boot/uefi/
arch/x86_64/
kernel/{init,memory,panic}/
drivers/{console,input,storage/ata_pio}/
block/{device,cache}/
vfs/
fs/inferencefs/
shell/commands/
lib/
include/{inferenceos,inferencefs}/
cmake/toolchains/
tools/
docs/{format,commands,limitations}/
tests/{unit,integration,corruption,fault,support}/
```

**Structure Decision**: One freestanding kernel tree separated by architectural and subsystem boundaries, plus host tools/tests. Pure logic compiles both freestanding and natively. Filesystem internals stay under `fs/inferencefs`; generic callers see only block and VFS headers.

## Toolchain and Release Manifest

Reference CI pins GCC 16.2, Clang/LLVM 22.1.8, GNU binutils 2.45, QEMU 11.1.x (initially 11.1.0), edk2 `edk2-stable202605`, CMake 4.1.x, Ninja 1.12.x, and Python 3.12.x. Patch upgrades require the full dual-compiler/QEMU suite. The release manifest records exact versions, firmware SHA-256, source revision, flags, and artifact checksums. Release candidates are excluded.

Use a checked-in minimal UEFI ABI plus an isolated assembly calling-convention trampoline. Do not add `ms_abi` annotations unless the specification’s extension allowlist is amended.

## Complexity Tracking

No entries: the design passes all constitution gates.
