# Phase 0 Research: InferenceOS CUI/GUI Filesystem Demonstrator

## R1 — Boot and Kernel Handoff

**Decision**: Build a project-owned PE32+ UEFI loader at `EFI/BOOT/BOOTX64.EFI`. It loads and
validates an ELF64 kernel from the separate FAT EFI System Partition, captures the final UEFI
memory map and GOP framebuffer in a versioned boot-information structure, calls
`ExitBootServices`, and transfers to the kernel. COM1 diagnostics begin at kernel entry.

**Rationale**: This keeps firmware storage separate from InferenceOS-FS while preserving ELF
symbols and a small explicit boot contract.

**Alternatives considered**: Limine and GRUB add an external boot contract; an EFI-stub kernel
couples firmware entry and kernel image concerns.

## R2 — Toolchains and Reference VM

**Decision**: Use one freestanding C17 source graph validated by `x86_64-elf-gcc`/GNU binutils and
`clang --target=x86_64-none-elf`/LLD. Use a shared linker script, no hosted runtime, no red zone,
and no SIMD until save/restore support exists. The reference VM is QEMU `q35` with OVMF and TCG,
explicit devices, a separate ESP, a sparse 64 GiB raw persistent disk, and serial logging.

The reproducible bootstrap and `tools/bootstrap/versions.json` MUST use this version matrix:

| Component | Pinned version |
|---|---|
| GNU binutils | 2.45 |
| GCC | 16.2.0 |
| Clang and LLD | 22.1.8 |
| QEMU | 11.1.0 |
| OVMF/edk2 | edk2-stable202605 |
| CMake | 4.1.1 |
| Ninja | 1.13.1 |

The bootstrap MUST reject a mismatched existing binary or build/install the pinned project-local
version; it MUST NOT silently substitute a newer host version. Patch-level changes require an
explicit update to this decision and to the version manifest.

**Rationale**: Matching profiles expose portability and layout drift; TCG provides a deterministic
host-independent validation baseline.

**Alternatives considered**: Host compilers risk target contamination; one compiler violates the
constitution; host acceleration remains optional because it is not deterministic across CI hosts.

## R3 — Graphics, Input, and Windowing

**Decision**: Request 1024x768 BGRX8888 through OVMF GOP on QEMU standard VGA and pass the actual
stride to a framebuffer abstraction. Use a licensed 8x16 PSF2 bitmap font, i8042 PS/2 keyboard and
relative mouse drivers feeding one normalized input queue, and a retained software window manager
with per-window XRGB surfaces, z-order, clipping, focus, dirty rectangles, and a shadow buffer.
Failure to obtain the exact reference mode returns to the usable CUI.

**Rationale**: This is the smallest deterministic stack satisfying pixel, text, pointer, movable
window, terminal, File Explorer, and recovery requirements.

**Alternatives considered**: Virtio-GPU, USB HID, scalable fonts, alpha compositing, and direct
framebuffer drawing add driver/parser complexity or weaken window ownership.

## R4 — Storage Controller and Cache

**Decision**: Use PCI virtio-blk with required flush support behind a controller-independent
512-byte block API. Implement a sector-keyed write-back cache with dirty/error/I/O state, pinning,
and generation barriers. A barrier writes all sectors through its generation, waits for completion,
then issues and waits for the device flush. Failed writeback remains dirty and fails the barrier.

**Rationale**: Virtio-blk offers capacity and flush semantics with less first-release complexity
than AHCI or NVMe; generation barriers express required data/metadata ordering.

**Alternatives considered**: AHCI/NVMe are larger drivers; IDE is obsolete; write-through and
uncached I/O cannot efficiently provide the required coherent dirty-state behavior.

## R5 — Filesystem Geometry

**Decision**: Freeze the specification's version-1 geometry: little-endian, 512-byte sectors,
4096-byte clusters, superblocks at sectors 0 and 1, one FAT at sector 2, a fixed 4096-sector
registry, then data with root cluster 2. Compute FAT length to a fixed point and reject overflow,
overlap, out-of-range clusters, or insufficient FAT entries.

**Rationale**: This exactly implements the approved on-disk contract without inventing alignment
gaps or redundancy absent from the superblock.

**Alternatives considered**: MiB alignment, two FATs, larger clusters, and dynamic registry sizing
all require a format amendment.

## R6 — Mutation and Recovery Ordering

**Decision**: Use an uncommitted companion as a visibility fence. Creation/save persists an
uncommitted companion, initialized data, FAT state, final primary, then committed companion, with
flush barriers between dependency groups. Rename uncommits the companion before changing the
primary and recommits only after association/hash integrity is valid. Delete uncommits, removes
namespace records, then releases clusters. Do not claim transactional rollback without a journal.

**Rationale**: A crash leaves either a valid pair or a detectable incomplete state and never a
healthy-looking mismatched pair or reused live clusters.

**Alternatives considered**: Primary-first mutation exposes mismatches; create-new/delete-old risks
duplicate ownership; journaling and copy-on-write require new on-disk structures.

## R7 — Mount States

**Decision**: Mount as healthy read-write, bounded diagnostic read-only, or rejected. Read-write
requires matching valid superblocks, safe geometry/root traversal, and no mutation-unsafe anomaly.
Diagnostic mode is allowed when bounds are trustworthy but metadata is inconsistent. Reject when
bounds or traversal cannot be proven. Registry corruption merely disables the registry. Version 1
does not repair automatically.

**Rationale**: This preserves diagnostic observability without risking writes through ambiguous
allocation ownership.

**Alternatives considered**: Silent repair risks data loss; writable mount with hidden corruption
is unsafe; rejecting every imperfect disk removes useful bounded diagnostics.

## R8 — Execution, Syscall, and IPC Model

**Decision**: Run the kernel in ring 0 and statically linked Shell, GUI, File Explorer, and test
applications in ring 3. Use x86-64 `syscall`/`sysret`: number/result in RAX; arguments in RDI, RSI,
RDX, R10, R8, R9; stable negative errors; size/version-prefixed structures; copy-in/copy-out; and
strict pointer/range/version validation. Shell mediation uses versioned kernel IPC and a trusted
well-known service capability.

**Rationale**: Real privilege and IPC boundaries make pointer, identity, handle, and metadata
isolation testable while avoiding full production process scope.

**Alternatives considered**: Ring-0 applications cannot prove isolation; function linking is not
mediation; a full dynamic/preemptive production environment exceeds scope.

## R9 — Opaque Handles and Safe Metadata

**Decision**: Use process-local 64-bit index/generation handles with object kind and rights. Kernel-
minted process-scoped type capabilities authorize icon/routing operations. One display-safe DTO
permits opaque identity, extension-free name, object kind, size, generic attributes, type/icon
capability, and allowed operations; it forbids canonical name, extension/hash, record and cluster
locations. Diagnostics use a separate privileged capability and DTO.

**Rationale**: Structural separation prevents accidental leaks and fabricated/stale handles from
expanding rights. Exact extension verification remains internal.

**Alternatives considered**: Raw pointers/addresses are forgeable; global IDs leak state; hash type
IDs expose forbidden metadata and preserve collision ambiguity.

## R10 — CUI Contract

**Decision**: Use one command engine for standalone CUI and GUI terminal. Version 1 supports a
255-byte line, printable ASCII, Backspace/Enter, whitespace tokens, double quotes, and `\"`/`\\`
escapes; it has no pipes, expansion, redirection, globbing, or scripting. Errors use stable symbols
and return to the prompt. Ordinary `dir` consumes safe DTOs; diagnostics require authority.

**Rationale**: One parser prevents semantic drift and supports deterministic serial automation.

**Alternatives considered**: Separate parsers drift; POSIX shell syntax adds unrelated scope.

## R11 — Validation and Fault Injection

**Decision**: Use host unit/property tests for pure algorithms, hosted integration tests over an
in-memory/sparse block backend, and headless QEMU black-box suites over serial plus framebuffer
checkpoints. Validation builds provide deterministic faults at block, allocator, persistence,
metadata, GUI/input, syscall, and IPC boundaries. Both compilers run translation-unit and host
tests; primary-toolchain artifacts run the full QEMU suite.

**Rationale**: Fast layers localize failures while real boot, privilege, drivers, persistence, GUI,
and reboot behavior remain end-to-end tested.

**Alternatives considered**: QEMU-only is slow; host-only cannot prove hardware boundaries; manual
demos and random chaos are not reproducible.

## R12 — Extension Registry Benchmark Gate

**Decision**: Keep the registry disabled by default. Compare enabled/disabled runtime modes on
cloned deterministic images and identical cold/warm query traces. Under TCG, count instructions and
conditional branches between guest markers; report hardware cycles only from a separate pinned-host
accelerated profile. Report median, p95, spread, versions, image checksum, seed, and update costs.
Require no correctness differences and at least 10% median instruction or end-to-end latency
improvement with no more than 5% median durable-save regression before proposing default enablement.

**Rationale**: A predefined matched test avoids moving the gate after observing results and keeps
cycle claims distinct from emulator instruction counts.

**Alternatives considered**: Wall-clock-only, different images, registry-only microbenchmarks, or
TCG “cycle” claims cannot substantiate the proposed benefit.

## R13 — Process Scheduling and Lifecycle

**Decision**: Use a single-core preemptive round-robin scheduler driven by the local APIC timer,
with fixed-priority kernel threads, equal-priority ring-3 processes, blocking wait queues, and an
idle thread. Each statically linked ring-3 application is a separate ELF64 process image with its
own page tables, kernel stack, user stack, immutable application identity, handle table, and IPC
endpoints. Version 1 has no fork, dynamic linking, multi-core scheduling, or user-selectable
priority. A process blocks on IPC, input, timer, or explicit yield and exits through a versioned
syscall; exit closes handles, cancels IPC waits, releases pages, and makes the status collectable.

Discover the local APIC through ACPI MADT, require one enabled bootstrap processor, mask and remap
the legacy 8259 PIC before enabling APIC interrupts, and calibrate the local APIC timer against the
ACPI PM timer. Use a 10 ms ring-3 quantum. Kernel threads handling input, storage completion, and
CUI recovery run at fixed priorities above ordinary ring-3 work but block when idle; interrupt
handlers only acknowledge hardware and queue deferred work.

**Rationale**: Preemption prevents a faulty GUI or application from starving CUI recovery, while
wait queues make Shell, GUI, and application IPC/input behavior implementable without busy loops.
Separate address spaces make the selected pointer, identity, and metadata boundaries meaningful.

**Alternatives considered**: Cooperative-only scheduling cannot preserve recovery if a ring-3
client fails to yield; running applications in one address space defeats pointer/handle isolation;
multi-core scheduling and full POSIX process semantics exceed first-release scope.

## R14 — Initial Ring-3 System Module Pack

**Decision**: Package Shell, GUI desktop, File Explorer, proprietary test application, and custom
test application as separate statically linked ELF64 files on the FAT ESP under
`/InferenceOS/System/`. A versioned `/InferenceOS/System/modules.manifest` lists each required
module's immutable application identity, role, path, byte length, entry ABI version, and SHA-256
digest. The UEFI loader reads the manifest, rejects duplicates/unknown required roles, validates
paths, sizes, ELF64 structure, and digests, allocates pages, and copies every required module before
`ExitBootServices`. Versioned module descriptors in boot information identify immutable memory
ranges; the kernel revalidates bounds/digests, maps each image into a private address space, and
starts services in order: Shell, GUI desktop/terminal, File Explorer, then optional test apps.

The module pack is a boot-time integrity and reproducibility mechanism, not a secure-boot or signed
publisher claim. A missing or invalid required Shell module is a boot failure reported through
serial. A missing/invalid GUI desktop or terminal module leaves the CUI usable and reports GUI
unavailability; an absent optional File Explorer module disables only File Explorer until that
story is installed. After copying, ring-3 processes do not retain ESP access.

**Rationale**: Loader-supplied modules allow CUI, Shell, and GUI startup before InferenceOS-FS is
mounted, preserve separate process images, and provide deterministic identities without embedding
application binaries inside kernel code.

**Alternatives considered**: Loading initial services from InferenceOS-FS creates a bootstrapping
cycle; embedding all apps into the kernel weakens process/artifact boundaries; an initramfs adds a
second archive format; code signing and Secure Boot require separate trust-policy scope.
