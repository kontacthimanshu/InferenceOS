# InferenceOS Demonstrator Limitations

InferenceOS is an experimental, single-machine operating-system and filesystem demonstrator. It is
not production-ready, hardened, or a substitute for a general-purpose operating system.

## Supported reference boundary

- x86-64 UEFI under pinned QEMU 11.1.0 with q35, OVMF, TCG, one `qemu64` CPU, standard VGA GOP,
  PS/2 input, and virtio-blk persistent storage.
- One standalone CUI recovery path and one GUI desktop over a shared VFS namespace.
- One InferenceOS-FS root volume of at least 50,000,000,000 bytes, with 512-byte sectors,
  4096-byte clusters, version-1 fixed layouts, and internal FAT32-style 8.3 names.
- Statically linked, packaged ELF64 system applications using the versioned InferenceOS interfaces.

Other firmware, physical hardware, accelerators, CPU counts, graphics/input devices, storage
controllers, and host QEMU versions are not release-qualified merely because they happen to boot.

## Filesystem limitations

- InferenceOS-FS is a distinct filesystem. It is not FAT32-compatible even though it derives its
  allocation and primary directory-record model from FAT32.
- Maximum file size is 4 GiB minus one byte. Sparse guest files are not supported in version 1.
- Version 1 has no journal, snapshots, encryption, compression, or automatic repair.
- Corruption can result in diagnostic read-only access or a rejected mount. The system does not
  silently repair unsafe metadata or promise recovery of damaged data.
- CRC-32 and extension hashes detect specified integrity/association errors; neither is a
  cryptographic security mechanism. Hash collisions never establish file identity.
- The reference persistent image is host-sparse. Tools that do not preserve sparse ranges can
  expand it to its full logical size.

## Extension hiding is not secrecy

Ordinary CUI listings, GUI File Explorer views, and application contracts hide raw file extensions
and extension hashes. This is an interface and mediation rule, not encryption or an access-control
boundary. Privileged diagnostics and raw-disk analysis can reveal all internal names and metadata.
InferenceOS therefore makes no claim that extension hiding protects file contents or prevents an
attacker with diagnostic or storage access from learning file types.

Proprietary application examples demonstrate routing and capability rules only. They do not mean
Microsoft Word, Excel, PDF implementations, or other proprietary formats have been ported. The OS
does not invent, reverse engineer, or endorse unofficial proprietary APIs.

## Research-gated registry

The on-disk Extension Registry region exists, but registry optimization is disabled by default.
Correctness comes from authoritative directory metadata and exact extension verification. Registry
absence, corruption, staleness, exhaustion, or disablement must fall back to directory scanning.

Any performance benefit remains a hypothesis until the prescribed matched enabled/disabled
benchmark reports instructions, conditional branches, end-to-end latency, spread, and separately
qualified TCG or hardware-cycle measurements. No branch, cycle, or latency improvement is claimed
by this release documentation.

## Deferred capabilities

The first demonstrator does not provide:

- networking or network-complete services;
- multi-user isolation or a production security model;
- POSIX compatibility or full POSIX process semantics;
- dynamic linking, package management, or a general third-party application ecosystem;
- journaling, snapshots, encryption, compression, or automatic filesystem repair;
- multi-core scheduling or qualification on physical hardware;
- Secure Boot, signed module provenance, or cryptographic storage integrity.

The scheduler, process isolation, capabilities, syscalls, and diagnostic authorization are designed
to demonstrate architecture and contracts. They have not undergone the security review, adversarial
testing, hardware qualification, or long-duration reliability work required for production claims.

## Reproducibility boundary

Reproducibility claims apply to the pinned tool versions, controlled inputs, deterministic image
recipes, and documented GCC/Clang validation profiles. Image manifests contain no timestamps or
absolute source paths. QEMU evidence manifests intentionally contain run times and host artifact
paths because they record a particular validation run.

The present CMake cross-build compiles the production libraries and UEFI loader but does not yet
link the final kernel or static Shell/GUI application executables. The ESP packaging target therefore
requires explicitly supplied kernel and system-module inputs, as described in `build.md`. Until
those final link targets are integrated, the repository does not claim that an external evaluator
can produce the complete bootable image solely from a clean checkout.

See [build.md](build.md) for the qualified build, launch, and validation workflow.
