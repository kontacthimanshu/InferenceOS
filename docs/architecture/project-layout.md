# InferenceOS Project Layout and Ownership

This document defines the repository boundaries established by T001. Ownership here means responsibility for code and policy, not individual maintainers. Dependencies must follow the direction documented below so ordinary file operations cannot bypass the VFS or generic block-device layers.

## Dependency Direction

```text
shell/commands
      |
      v
     vfs
      |
      v
fs/inferencefs
      |
      v
 block/cache
      |
      v
 block/device
      |
      v
drivers/storage/ata_pio
```

Boot, architecture, kernel runtime, console, and input components support this path but do not own filesystem policy. Filesystem-specific diagnostics use a separate validated read-only interface; they do not replace ordinary VFS operations.

## Production Directories

| Path | Ownership and boundary |
|---|---|
| `boot/uefi/` | UEFI application entry, checked kernel loading, firmware handoff, and isolated firmware ABI bridge. No post-`ExitBootServices` filesystem policy. |
| `arch/x86_64/` | x86-64 entry, descriptor tables, exceptions, CPU control, linker layout, and unavoidable assembly. No VFS or InferenceFS policy. |
| `kernel/init/` | Top-level initialization sequencing and validated boot-information consumption. |
| `kernel/memory/` | Physical-page allocation, bounded heap, and fixed kernel object pools. |
| `kernel/panic/` | Bounded fatal-error reporting and safe halt behavior. |
| `drivers/console/` | Character output devices and console fan-out, including early serial diagnostics. |
| `drivers/input/` | Bounded character input devices, initially polling PS/2 keyboard input. |
| `drivers/storage/ata_pio/` | PIIX-compatible ATA PIO controller commands, polling, timeouts, and status decoding behind the block-device contract. |
| `block/device/` | Generic synchronous block-device registry and transport-neutral read/write/flush/query contract. |
| `block/cache/` | Fixed-capacity sector cache, dirty state, ordered writeback, and error retention. |
| `vfs/` | Opaque mounts and file/directory handles, path resolution, generic filesystem operations, and mount lifecycle. |
| `fs/inferencefs/` | InferenceFS-FAT32 format, mount validation, FAT, directory records, companion metadata, file operations, and diagnostic adapter. |
| `shell/commands/` | Thin character-prompt command handlers. Ordinary file commands call VFS only. |
| `lib/` | Small freestanding, policy-neutral runtime and algorithms reusable by host tests. |
| `include/inferenceos/` | Public generic kernel, driver, block, VFS, and runtime contracts. |
| `include/inferencefs/` | Versioned InferenceFS-FAT32 layouts and filesystem-specific diagnostic contracts. |

## Build, Tooling, and Documentation

| Path | Ownership and boundary |
|---|---|
| `cmake/toolchains/` | Pinned GCC and Clang freestanding target profiles. |
| `tools/` | Host-only build, image, manifest, launch, and inspection utilities. Host dependencies never leak into kernel targets. |
| `docs/architecture/` | Dependency, ownership, and design-boundary documentation. |
| `docs/build/` | Reproducible build, toolchain, firmware, and host setup documentation. |
| `docs/format/` | Independently usable InferenceFS-FAT32 on-disk format documentation. |
| `docs/commands/` | Character command syntax, results, and diagnostics. |
| `docs/limitations/` | Explicit first-release limitations and deferred functionality. |
| `docs/validation/` | Generated or reviewed requirement traceability and release evidence. |

## Test Directories

| Path | Ownership and boundary |
|---|---|
| `tests/unit/` | Host-native C17 tests for pure algorithms and bounded components. |
| `tests/integration/` | Headless QEMU/OVMF boot, command, ATA, lifecycle, and persistence scenarios. |
| `tests/corruption/` | Deterministic malformed-image fixtures and mount/diagnostic expectations. |
| `tests/fault/` | Interrupted-write and injected block-operation failure scenarios. |
| `tests/support/` | Test-only runners, memory devices, fault decorators, fixtures, and harness helpers; excluded from release kernel targets. |

## Boundary Rules

1. `shell/commands/` must use `vfs/` for ordinary file and directory operations.
2. `vfs/` may call a registered filesystem adapter but must not interpret FAT entries or raw directory records.
3. `fs/inferencefs/` must use `block/device/` through `block/cache/`; it must not access ATA ports.
4. `drivers/storage/ata_pio/` must not interpret filesystem structures.
5. Extension hashes are derived metadata. Exact canonical extension text and the complete primary name remain authoritative.
6. Production code must not depend on `tests/` or host-only `tools/`.
7. Assembly is confined to `boot/uefi/` and `arch/x86_64/` and must not contain VFS, filesystem, hashing, CRC, filename, or command policy.
8. Generated artifacts belong under ignored build directories and must not be written into source-owned paths.
