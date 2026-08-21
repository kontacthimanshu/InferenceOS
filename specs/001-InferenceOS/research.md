# Phase 0 Research: InferenceOS Minimal

## Decisions

### Boot and firmware boundary

- **Decision**: A small project-owned `BOOTX64.EFI` on a separate FAT32 boot image loads a fixed-address ELF64 kernel, captures the UEFI memory map and GOP data, exits boot services, and transfers through an x86-64 assembly entry/ABI shim. COM1 is the earliest diagnostic sink.
- **Rationale**: Keeps the kernel freestanding and the firmware boundary small while satisfying UEFI boot and separate boot/data media.
- **Alternatives considered**: A monolithic UEFI kernel couples runtime design to firmware; GRUB/Limine adds a boot dependency; `ms_abi` annotations are outside the approved extension allowlist.

### Toolchain and reproducibility

- **Decision**: Pin GCC 16.2, Clang/LLVM 22.1.8, binutils 2.45, QEMU 11.1.x, and edk2-stable202605. Use deterministic archives, path-prefix maps, a controlled `SOURCE_DATE_EPOCH`, stable image metadata, checksums, and a generated release manifest.
- **Rationale**: Satisfies FR-166 and makes clean-build results auditable under two compiler families.
- **Alternatives considered**: Floating distro/latest tools are not reproducible; release candidates are unsuitable; committing generated images risks source/artifact drift.

### Execution and memory model

- **Decision**: One synchronous execution context with polling I/O, a physical-page allocator, and a small bounded heap/fixed pools; no scheduler.
- **Rationale**: Sufficient for the prompt/filesystem demonstration and constitutionally minimal.
- **Alternatives considered**: Multitasking and asynchronous I/O add lifecycle and synchronization work without required value.

### Console, keyboard, and shell

- **Decision**: GOP framebuffer text output with COM1 diagnostic mirroring, polling i8042/PS/2 keyboard input, a 256-byte fixed line buffer, and a static strict-arity command table. `write`/`append` consume the remainder after the path as text.
- **Rationale**: Proves display and keyboard requirements, preserves diagnostics, and keeps parsing bounded.
- **Alternatives considered**: UEFI protocols after `ExitBootServices`, interrupt input, serial-only prompt, or a general quoting language.

### Storage and cache

- **Decision**: Discover one fixed reference ATA disk with IDENTIFY, use bounded LBA28 PIO commands and CACHE FLUSH behind the generic block interface, and place a fixed 64-entry sector cache above it. Dirty write/eviction failures remain dirty and observable.
- **Rationale**: Covers the 1 GiB maximum with the smallest specified i440fx/PIIX stack and bounded 32 KiB cache storage.
- **Alternatives considered**: AHCI/VirtIO/UEFI Block I/O conflict with scope or architecture; DMA/interrupt I/O and background writeback require unnecessary machinery.

### Format geometry and mount validation

- **Decision**: Solve FAT size iteratively with checked 64-bit arithmetic, initialize FAT/root before backup and primary superblocks, and mount as clean-writable, diagnostic-read-only, or rejected. Before writable exposure, validate both superblocks, geometry, reserved FAT entries, reachable namespaces, entry pairs, chains, and cross-links with a bounded ownership bitmap.
- **Rationale**: Incomplete format stays invalid; no untrusted value drives I/O; a 1 GiB volume needs only about 32 KiB for one ownership bit per 4 KiB cluster.
- **Alternatives considered**: Fixed FAT sizes, primary-first format, lazy writable validation, or automatic repair.

### FAT and directories

- **Decision**: Centralize checked FAT get/set/allocate/free/walk operations. Bound walks by data-cluster count and use a non-authoritative rotating free hint. Parse each directory slot into an explicit kind; form a file only from a valid committed companion immediately followed by a regular primary in the same cluster.
- **Rationale**: Prevents loops/out-of-range access and accidental interpretation of companion bytes as filenames.
- **Alternatives considered**: Heuristic companion association, hash-only equality, persistent FSInfo hints, or unbounded scans.

### Mutation and durability

- **Decision**: Use ordered non-journaled transitions with explicit barriers. Create commits companion last; rename hides the pair before updating and recommitting; delete hides before freeing; expansion zeroes data before linking and publishes size last. Only completed required barriers permit durability acknowledgement.
- **Rationale**: Interruptions yield detectable incomplete state rather than a healthy mismatched file, without introducing journaling.
- **Alternatives considered**: Journaling/COW exceeds scope; metadata-first ordering exposes stale data; freeing before hiding risks cross-file corruption.

### VFS and diagnostics

- **Decision**: Opaque synchronous mount/node/file handles and typed result codes form the generic interface. Filesystem-specific inspection is a separate read-only diagnostic contract.
- **Rationale**: Commands remain above VFS while the experimental format stays observable.
- **Alternatives considered**: Raw record/LBA exposure, global POSIX descriptors, or generic untyped ioctl diagnostics.

### Testing

- **Decision**: Compile pure algorithms against a memory block device as host C17 tests under both compilers; drive actual PS/2 input through QMP in headless QEMU; mutate images deterministically and inject read/write/flush failures by operation and LBA.
- **Rationale**: Combines fast exhaustive logic coverage with real firmware, keyboard, ATA, persistence, and interruption coverage.
- **Alternatives considered**: QEMU-only tests are slow; host-only tests cannot prove integration; timed VM termination and natural collision hunting are nondeterministic.

### License

- **Decision**: Record MIT as selected.
- **Rationale**: The repository already contains a complete MIT `LICENSE`; the spec’s statement that selection remains future work is stale.
- **Alternatives considered**: Changing licensing is outside this planning workflow.

## Clarification Result

No `NEEDS CLARIFICATION` item remains and no user decision blocks design.
