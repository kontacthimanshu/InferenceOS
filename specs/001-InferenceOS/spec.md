# Feature Specification: InferenceOS Minimal InferenceFS-FAT32 Demonstrator

**Feature Branch**: `001-inferencefs-fat32-demo`  
**Created**: 2026-08-21  
**Status**: Draft  
**Input**: User description: "Build a minimal operating system with a character command prompt, a mandatory VFS backed by InferenceFS-FAT32, and a companion directory entry containing a deterministic hash of each regular file's extension. Implement project-owned code in C17 in a freestanding environment, using a carefully controlled subset of GCC/Clang extensions plus a small amount of x86-64 architecture-specific assembly."

## Overview

InferenceOS Minimal is a bootable, open-source operating-system demonstrator whose purpose is to make the InferenceFS-FAT32 filesystem experiment visible and repeatable.

The first usable release boots as an x86-64 UEFI guest under QEMU, enters a character command prompt, exposes a persistent data disk through a minimal Virtual Filesystem (VFS), formats and mounts that disk as InferenceFS-FAT32, and supports a complete file lifecycle.

InferenceFS-FAT32 derives its allocation model from FAT32 but is a distinct filesystem. Regular files use a two-record directory entry set:

```text
+----------------------------------+
| Extension-hash companion record |
| 32 bytes                        |
+----------------------------------+
| FAT32-derived primary record    |
| 32 bytes                        |
+----------------------------------+
```

The primary record remains authoritative for the visible 8.3 filename and file metadata. The immediately preceding companion record stores a deterministic 32-bit hash of the canonical file extension plus versioning, association, and integrity information. The hash is derived metadata. It is derived from file's extension using suitable algorithm and whenever a file is requested, the search is performed using the hash and not by mere comparison of file extension. File extension is just used for display purposes. The has is never exposed to user.

The implementation baseline is freestanding ISO C17. Compiler-specific behavior is restricted to an explicit GCC/Clang extension allowlist. x86-64 assembly is limited to architecture mechanisms that cannot reasonably be expressed in C.

---

## User Scenarios & Testing (mandatory)

### User Story 1 - Boot to an interactive InferenceOS command prompt (Priority: P1)

As a user or filesystem evaluator, I can boot the reference InferenceOS image under QEMU and reach a usable character prompt so that the filesystem experiment can be exercised without a host debugger or graphical desktop.

**Why this priority**: No filesystem behavior can be demonstrated until the kernel boots and accepts commands.

**Independent Test**: Start the documented QEMU x86-64 UEFI profile with a clean boot image and reference data disk; verify that the kernel displays its banner and `InferenceOS>` prompt, accepts keyboard input, executes `help`, and remains responsive after an unknown command.

**Acceptance Scenarios**:

1. **Given** the documented QEMU profile and a valid boot image, **When** the virtual machine starts, **Then** InferenceOS reaches the interactive command prompt without requiring a graphical desktop or user login.
2. **Given** the command prompt is active, **When** the user types printable ASCII characters, Backspace, and Enter, **Then** command-line editing and submission behave deterministically.
3. **Given** an unknown command, **When** it is submitted, **Then** the system reports an unknown-command error and returns to the prompt.
4. **Given** a fatal early boot error, **When** the kernel cannot safely continue, **Then** an explanatory diagnostic is emitted to the earliest available console and serial output.

---

### User Story 2 - Format and mount an InferenceFS-FAT32 data disk through the VFS (Priority: P1)

As a user or evaluator, I can format the designated persistent data disk as InferenceFS-FAT32 and mount it as the VFS root so that all ordinary file commands operate through the generic filesystem interface.

**Why this priority**: The VFS-backed custom filesystem is the central architectural claim of the demonstrator.

**Independent Test**: Attach a blank supported QEMU data disk, boot InferenceOS, run the formatting command, mount the volume at `/`, run `fsinfo`, and verify that the VFS identifies InferenceFS-FAT32 as the mounted root filesystem.

**Acceptance Scenarios**:

1. **Given** an unformatted supported data disk, **When** the user explicitly formats it as InferenceFS-FAT32, **Then** a valid version-1 filesystem is created with the required superblock, FAT region, root directory cluster, and backup superblock.
2. **Given** a valid InferenceFS-FAT32 volume, **When** it is mounted, **Then** the VFS exposes it at `/`.
3. **Given** the volume is mounted, **When** `fsinfo` is executed, **Then** the filesystem identity, format version, logical sector size, cluster size, FAT geometry, root cluster, directory-record size, companion-record size, and extension-hash algorithm are displayed.
4. **Given** an unsupported or corrupt filesystem signature or version, **When** mount is requested, **Then** the volume is not mounted writable and a specific diagnostic is reported.
5. **Given** the filesystem is mounted, **When** generic file commands execute, **Then** they access files through VFS operations rather than directly invoking raw FAT or block-device operations.

---

### User Story 3 - Create, write, read, list, rename, and delete persistent files (Priority: P1)

As a user, I can perform a complete file lifecycle through character commands so that InferenceFS-FAT32 behaves as a usable persistent filesystem rather than only as an on-disk structure experiment.

**Why this priority**: File lifecycle behavior is necessary to prove that the VFS, allocation table, directory records, and block storage work together.

**Independent Test**: Mount a clean volume, create `TEST.TXT`, write known text, read it back, list the directory, rename it, append content, reboot, verify content, delete it, and verify that its allocated clusters become reusable.

**Acceptance Scenarios**:

1. **Given** a writable mounted root, **When** `create TEST.TXT` executes, **Then** a zero-length regular file is created with one valid hash companion record and one valid primary file record.
2. **Given** `TEST.TXT` exists, **When** known bytes are written, **Then** required clusters are allocated, the file size is updated, and reading returns the same bytes.
3. **Given** a file spans multiple clusters, **When** it is read sequentially, **Then** its cluster chain is followed in order and the complete acknowledged file content is returned.
4. **Given** a directory contains files and subdirectories, **When** `dir` executes, **Then** each visible object is listed once and companion records are not exposed as separate files.
5. **Given** a file is renamed without changing its extension, **When** the rename succeeds, **Then** the visible name changes while the canonical extension and extension hash remain unchanged.
6. **Given** a file is deleted, **When** deletion succeeds, **Then** both directory records are released or marked deleted and all clusters owned solely by that file become free.
7. **Given** a requested operation cannot complete because the disk is full, **When** the operation fails, **Then** a defined no-space error is returned and previously acknowledged file content remains intact.

---

### User Story 4 - Demonstrate the extension-hash companion record (Priority: P1)

As an evaluator, I can inspect the companion record for a regular file so that I can directly observe the defining InferenceFS-FAT32 extension-hash concept.

**Why this priority**: The extra extension-hash record is the primary experimental feature of the filesystem.

**Independent Test**: Create `TEST.TXT`, run `hashinfo TEST.TXT`, verify the canonical input `TXT`, FNV-1a-32 algorithm identifier, stored hash, record location, primary-record association, and validation status.

**Acceptance Scenarios**:

1. **Given** `TEST.TXT`, **When** its hash metadata is created, **Then** the hash input is the canonical bytes `TXT` and does not include the base filename or separator dot.
2. **Given** `test.txt` is accepted as user input, **When** it is created, **Then** the stored short name and hash canonicalization use the documented uppercase representation.
3. **Given** two files with the same extension, **When** their companion records are inspected, **Then** they contain the same extension hash for the same algorithm and canonicalization version.
4. **Given** two files with different extensions that happen to have the same hash, **When** lookup or enumeration occurs, **Then** the files remain distinct because the original extension text remains authoritative.
5. **Given** a regular file primary entry, **When** directory scanning validates it, **Then** the immediately preceding record must be a valid committed companion whose association checksum matches the primary short name.
6. **Given** a companion record, **When** its CRC is recalculated, **Then** it matches the stored CRC before the record is trusted.

---

### User Story 5 - Rename a file across extensions and recompute derived metadata (Priority: P1)

As a user, I can rename a regular file from one extension to another and have its extension-hash metadata updated consistently.

**Why this priority**: The companion hash must remain correct through ordinary filesystem mutation, not only at file creation.

**Independent Test**: Create `TEST.TXT`, record its hash, rename it to `TEST.LOG`, inspect both records, and verify that the extension changes to `LOG`, the extension length and hash are recomputed, and the file contents remain unchanged.

**Acceptance Scenarios**:

1. **Given** `TEST.TXT`, **When** it is renamed to `TEST.LOG`, **Then** the primary name becomes `TEST    LOG` and the companion hash becomes FNV-1a-32 of canonical `LOG`.
2. **Given** `TEST.TXT`, **When** it is renamed to `REPORT.TXT`, **Then** the extension hash remains the FNV-1a-32 hash of `TXT` while the primary-name association checksum is updated.
3. **Given** a rename that cannot commit both required records consistently, **When** the operation fails, **Then** the old visible file remains valid or the volume is reported inconsistent; a half-renamed file MUST NOT be presented as successfully committed.
4. **Given** the destination filename already exists, **When** rename is attempted, **Then** the operation fails without overwriting the destination.

---

### User Story 6 - Persist files and hash metadata across reboot (Priority: P1)

As an evaluator, I can flush data, reboot, remount the filesystem, and verify both file contents and extension-hash metadata.

**Why this priority**: Persistence is necessary to demonstrate that the project implements an on-disk filesystem rather than an in-memory simulation.

**Independent Test**: Create multiple files, write known contents, run `sync`, reboot, remount, verify content and hash records, and compare `hashinfo` output before and after reboot.

**Acceptance Scenarios**:

1. **Given** file data and metadata have been acknowledged as flushed, **When** InferenceOS reboots and remounts the volume, **Then** the acknowledged contents and both required directory records are present.
2. **Given** a cleanly unmounted volume, **When** it is mounted again, **Then** no false corruption warning is reported.
3. **Given** pending dirty blocks, **When** `sync`, `reboot`, or `shutdown` completes, **Then** filesystem metadata is written in the documented order and the block device is flushed before restart or halt.
4. **Given** a file with an extension hash, **When** it is reopened after reboot, **Then** the companion record is revalidated from on-disk bytes rather than reconstructed silently without verification.

---

### User Story 7 - Inspect file, hash, and allocation structures from the command prompt (Priority: P2)

As an evaluator or developer, I can inspect the filesystem structures associated with a selected file so that the experiment is observable without attaching GDB or opening the image in a host hex editor.

**Why this priority**: Observability makes the filesystem innovation understandable and demonstrable.

**Independent Test**: Create a multi-cluster file and run `fileinfo`, `hashinfo`, and `fatinfo`; confirm that the commands expose the primary record, companion record, file size, first cluster, and complete cluster chain.

**Acceptance Scenarios**:

1. **Given** a valid regular file, **When** `fileinfo` executes, **Then** it displays the canonical filename, attributes, size, first cluster, primary-record location, and companion-record location.
2. **Given** a valid regular file, **When** `hashinfo` executes, **Then** it displays extension text, canonical hash input, hash algorithm, stored hash, companion version, association validation, and record CRC validation.
3. **Given** a non-empty file, **When** `fatinfo` executes, **Then** it displays the ordered cluster chain ending in an end-of-chain marker.
4. **Given** an empty file with no allocated data cluster, **When** `fatinfo` executes, **Then** it reports an empty chain rather than inventing a cluster.
5. **Given** a corrupted chain or companion record, **When** a diagnostic command inspects it, **Then** the command reports corruption and does not traverse unbounded or invalid storage.

---

### User Story 8 - Create and navigate directories (Priority: P2)

As a user, I can create and navigate a small hierarchical namespace so that the VFS and InferenceFS-FAT32 demonstrate directory behavior beyond the root directory.

**Why this priority**: Hierarchical directories demonstrate the VFS boundary while remaining small enough for the minimal OS.

**Independent Test**: Create `/DOCS`, change into it, create `/DOCS/NOTE.TXT`, return to `/`, list `/DOCS`, and remove the directory after deleting its contents.

**Acceptance Scenarios**:

1. **Given** a mounted root, **When** `mkdir DOCS` executes, **Then** a directory primary record and directory data cluster are created without a regular-file hash companion.
2. **Given** `/DOCS` exists, **When** `cd /DOCS` executes, **Then** the current directory changes and `pwd` reports `/DOCS`.
3. **Given** a current directory, **When** `.` or `..` is used in a path, **Then** path resolution follows the documented semantics without escaping above `/`.
4. **Given** a non-empty directory, **When** `rmdir` is attempted, **Then** the operation fails with directory-not-empty.
5. **Given** a directory needs more directory-entry slots, **When** no suitable free slot remains in the current cluster chain, **Then** the directory may allocate another cluster and continue scanning safely.

---

### User Story 9 - Detect malformed InferenceFS-FAT32 metadata safely (Priority: P2)

As a developer or evaluator, I can observe deterministic corruption detection so that invalid hash records, FAT chains, or superblocks are never silently treated as healthy data.

**Why this priority**: A custom filesystem must fail predictably when its unique metadata is missing or inconsistent.

**Independent Test**: Use host-side test-image mutation to create each required corruption case, boot the image, and verify the documented mount or diagnostic result.

**Acceptance Scenarios**:

1. **Given** a primary regular-file entry without a preceding valid hash companion, **When** the directory is scanned, **Then** the file is not treated as a healthy committed file and corruption is reported.
2. **Given** an orphaned hash companion not followed by a valid primary regular-file entry, **When** the directory is scanned, **Then** corruption is reported.
3. **Given** a companion with an unsupported version or algorithm identifier, **When** encountered, **Then** writable mount or mutation of the affected volume is refused.
4. **Given** a companion whose stored hash does not match the primary entry extension, **When** validated, **Then** hash metadata is reported inconsistent.
5. **Given** a FAT chain loop, out-of-range reference, bad-cluster misuse, or premature termination, **When** traversed, **Then** the operation stops with a corruption error and does not access outside the volume.
6. **Given** the primary and backup superblocks disagree, **When** mount occurs, **Then** the volume is not mounted writable until the inconsistency is explicitly resolved by tooling outside the first-release automatic-repair scope.

---

### User Story 10 - Rebuild and run the demonstrator from source (Priority: P2)

As an external developer, I can build the project from a clean checkout and launch the reference QEMU demonstration so that the filesystem claim is independently reproducible.

**Why this priority**: The project is open source and intended to demonstrate an idea to external observers.

**Independent Test**: On a documented supported development host, obtain the declared toolchain, run the single documented build workflow, launch the generated QEMU image, and execute the mandatory demonstration scenario.

**Acceptance Scenarios**:

1. **Given** the documented GCC-based cross-toolchain, **When** the project is built from a clean checkout, **Then** the bootable artifacts and blank/reference data-disk artifacts are reproducibly generated.
2. **Given** the documented Clang-based validation profile, **When** all C translation units are compiled, **Then** project-owned C code compiles under the supported Clang profile without relying on unapproved extensions.
3. **Given** a source file uses a compiler-specific extension, **When** it is reviewed or built, **Then** the extension is covered by the project extension allowlist and isolated through the documented portability boundary.
4. **Given** architecture-specific assembly, **When** inspected, **Then** it is confined to the x86-64 architecture layer and contains no VFS or InferenceFS-FAT32 policy logic.

---

## Edge Cases

The implementation and test suite MUST cover at least the following edge conditions:

- The root data disk is absent at boot.
- The selected block device reports a logical sector size other than 512 bytes.
- The disk is smaller than the minimum supported volume size or larger than the formatter's supported range.
- The superblock magic is wrong, partially written, or has an invalid CRC.
- The primary superblock is invalid while the backup superblock is valid.
- Both superblocks are valid but disagree.
- FAT geometry causes integer overflow or points beyond the physical disk.
- FAT entry 0, FAT entry 1, or the root-cluster entry contains an invalid value.
- A cluster chain loops back to an earlier cluster.
- A cluster chain references cluster 0, cluster 1, a reserved value, a bad cluster, or a cluster outside the data region.
- The last cluster in a file chain is not marked end-of-chain.
- The file size requires more data than its chain can supply.
- An empty regular file has a nonzero invalid first cluster.
- A regular file primary entry appears at the first directory slot with no room for a preceding companion.
- A hash companion appears as the final slot of a directory cluster without its following primary record.
- A hash companion is followed by a directory primary record instead of a regular-file primary record.
- The companion record version is unsupported.
- The companion hash algorithm identifier is unsupported.
- The companion record has nonzero reserved bytes in a version that requires them to be zero.
- The companion CRC is invalid.
- The companion primary-name checksum does not match the following primary record.
- The extension length does not match the canonical extension derived from the primary entry.
- The extension hash does not match the canonical extension.
- Two distinct extensions produce the same hash.
- Two files use the same exact 8.3 name in one directory.
- A file has no extension.
- A file has a one-, two-, or three-character extension.
- A name contains lowercase input and must be canonicalized.
- A name contains a second dot, more than eight base-name characters, more than three extension characters, a path separator, a space, a control byte, or an unsupported character.
- A directory path exceeds the maximum supported path length or directory depth.
- `..` is evaluated while the current directory is `/`.
- A regular-file creation has only one reusable directory slot available and therefore lacks two consecutive slots.
- A directory is full and requires chain extension.
- A file allocation succeeds but directory-record commit fails.
- Directory records commit but a required data-cluster allocation later fails.
- A write reaches the filesystem's maximum 32-bit file-size limit.
- Disk-full occurs while extending a fragmented file.
- A file is renamed while the destination already exists.
- Rename changes only case.
- Rename changes the base name but not the extension.
- Rename changes the extension.
- Deletion is interrupted after one of the two regular-file records is marked deleted.
- `sync` encounters a block-device error.
- Reboot or shutdown is requested with dirty blocks present.
- A diagnostic command is given a nonexistent file, directory instead of file, malformed path, or corrupted file.
- The command line exceeds the maximum input length.
- The block driver returns a timeout, short transfer, or error status.
- The kernel attempts an arithmetic calculation that overflows a 32-bit or 64-bit intermediate.
- A compiler inserts unexpected padding into an on-disk structure.
- GCC and Clang disagree on a compiler-extension macro or structure layout.
- An unsupported or unapproved compiler extension appears in project-owned source.

---

## Requirements (mandatory)

### Functional Requirements

#### Boot, Platform, Console, and Command Prompt

- **FR-001**: The first usable release MUST boot as an x86-64 UEFI guest under the documented QEMU reference profile.
- **FR-002**: The reference profile MUST use one virtual CPU.
- **FR-003**: The kernel MUST receive or establish sufficient boot information to initialize memory, console output, keyboard input, serial diagnostics, and the selected persistent data disk.
- **FR-004**: The system MUST provide a character command prompt as its primary interface.
- **FR-005**: The prompt MUST support printable ASCII input, Backspace, Enter, command tokenization, argument parsing, and deterministic error reporting.
- **FR-006**: The command line MUST support at least 255 input characters excluding the terminating null byte.
- **FR-007**: The first release MUST provide commands equivalent in purpose to `help`, `version`, `clear`, `devices`, `diskinfo`, `format`, `mount`, `unmount`, `fsinfo`, `sync`, `dir`, `cd`, `pwd`, `mkdir`, `rmdir`, `create`, `write`, `append`, `type`, `rename`, `delete`, `fileinfo`, `hashinfo`, `fatinfo`, `reboot`, and `shutdown`.
- **FR-008**: An unknown or malformed command MUST return control to the prompt without requiring reboot.
- **FR-009**: `shutdown` MUST flush required filesystem and block-device state before halting the virtual CPU.
- **FR-010**: `reboot` MUST flush required filesystem and block-device state before initiating the documented x86-64/QEMU restart mechanism.
- **FR-011**: Serial output MUST be available for boot, panic, storage, and filesystem diagnostics even when the primary character display is unusable.
- **FR-012**: The first release MAY poll keyboard and storage devices and MUST NOT require a preemptive scheduler or multitasking subsystem.

#### Reference QEMU Storage Profile

- **FR-013**: The reference machine profile MUST use QEMU's x86 PC/i440fx-compatible machine family with a PIIX-compatible IDE controller.
- **FR-014**: The persistent InferenceFS-FAT32 data disk MUST be accessed through an ATA PIO driver behind the generic block-device interface.
- **FR-015**: The persistent data disk MUST use a raw disk image and MUST expose 512-byte logical sectors.
- **FR-016**: InferenceFS-FAT32 MUST occupy the entire reference data disk beginning at logical sector 0; a partition-table implementation is not required for the first release.
- **FR-017**: The UEFI boot medium MUST be separate from the InferenceFS-FAT32 data volume and MAY use a standard FAT32 EFI System Partition or equivalent UEFI-readable boot image.
- **FR-018**: The formatter MUST reject a block device whose logical sector size is not exactly 512 bytes.
- **FR-019**: The first release MUST support formatting reference data volumes from 16 MiB through 1 GiB inclusive.
- **FR-020**: The default automated demonstration image MUST use a 64 MiB InferenceFS-FAT32 data disk.
- **FR-021**: The generic block-device interface MUST expose block read, block write, flush, logical sector size, sector count, and device status.
- **FR-022**: InferenceFS-FAT32 MUST NOT issue ATA controller I/O directly; it MUST use the generic block-device interface.

#### VFS

- **FR-023**: The kernel MUST provide a VFS abstraction between command code and InferenceFS-FAT32.
- **FR-024**: The first release MUST support a single persistent filesystem mount at `/`.
- **FR-025**: The VFS MUST define mount, unmount, create, open, close, read, write, seek, list-directory, create-directory, remove, rename, metadata-query, and flush operations.
- **FR-026**: VFS file and directory objects MUST be represented through opaque VFS references or handles rather than exposing raw filesystem-sector addresses to generic callers.
- **FR-027**: The command prompt MUST use VFS operations for ordinary file and directory commands.
- **FR-028**: Diagnostic commands MAY request filesystem-specific diagnostic information through an explicitly diagnostic interface but MUST NOT duplicate normal file operations by bypassing the VFS.
- **FR-029**: The VFS path separator MUST be `/`.
- **FR-030**: Absolute and relative paths MUST be supported.
- **FR-031**: `.` MUST identify the current directory and `..` MUST identify the parent directory; resolving `..` at `/` MUST remain at `/`.
- **FR-032**: The maximum supported path length MUST be at least 255 bytes including separators and excluding the null terminator.
- **FR-033**: The first release MUST support at least 16 directory levels including the root.
- **FR-034**: The VFS MUST prevent traversal outside the mounted root.

#### InferenceFS-FAT32 Volume Format

- **FR-035**: InferenceFS-FAT32 MUST be identified as a distinct filesystem and MUST NOT be advertised as standards-conforming FAT32.
- **FR-036**: Filesystem format version 1 MUST use little-endian encoding for all multi-byte on-disk integers.
- **FR-037**: Filesystem format version 1 MUST use a 512-byte logical sector and a fixed 4096-byte cluster consisting of eight logical sectors.
- **FR-038**: Filesystem format version 1 MUST reserve logical sector 0 for the primary superblock and logical sector 1 for the backup superblock.
- **FR-039**: Version 1 MUST use one File Allocation Table immediately following the two reserved superblock sectors.
- **FR-040**: The root directory MUST begin at cluster 2.
- **FR-041**: The data region MUST begin immediately after the FAT region.
- **FR-042**: Each FAT entry MUST occupy 32 little-endian bits, with the lower 28 bits carrying the allocation value and the upper four bits required to be zero in version 1.
- **FR-043**: FAT entry value `0x00000000` MUST mean free cluster.
- **FR-044**: FAT entry value `0x0FFFFFF7` MUST mean bad cluster.
- **FR-045**: FAT entry values `0x0FFFFFF8` through `0x0FFFFFFF` MUST mean end-of-chain.
- **FR-046**: FAT entry values `0x00000002` through `0x0FFFFFEF` MUST mean the next cluster in a chain when the value is a valid data-cluster number.
- **FR-047**: FAT entries 0 and 1 MUST be reserved and initialized to `0x0FFFFFF8` and `0x0FFFFFFF` respectively.
- **FR-048**: On a newly formatted volume, FAT entry 2 MUST mark the root-directory cluster end-of-chain.
- **FR-049**: The formatter MUST calculate a FAT size large enough to address every usable data cluster plus reserved FAT entries and MUST reject geometries whose calculations overflow or overlap.
- **FR-050**: Cluster-number validation MUST occur before every FAT or data-region access.

#### InferenceFS-FAT32 Superblock

- **FR-051**: The primary and backup superblocks MUST each occupy exactly one 512-byte logical sector.
- **FR-052**: The version-1 superblock MUST use the following fixed fields:

| Offset | Size | Field | Version-1 Value/Meaning |
|---:|---:|---|---|
| `0x000` | 8 | Magic | ASCII `INFFAT32` |
| `0x008` | 2 | FormatVersion | `1` |
| `0x00A` | 2 | HeaderSize | `64` |
| `0x00C` | 2 | BytesPerSector | `512` |
| `0x00E` | 1 | SectorsPerCluster | `8` |
| `0x00F` | 1 | FatCount | `1` |
| `0x010` | 2 | ReservedSectors | `2` |
| `0x012` | 2 | DirectoryEntrySize | `32` |
| `0x014` | 2 | HashEntrySize | `32` |
| `0x016` | 2 | Flags | `0` in version 1 |
| `0x018` | 4 | TotalSectors | Total sectors in volume |
| `0x01C` | 4 | SectorsPerFat | FAT length in sectors |
| `0x020` | 4 | RootCluster | `2` |
| `0x024` | 4 | VolumeSerial | Formatter-generated non-security identifier |
| `0x028` | 11 | VolumeLabel | Uppercase ASCII, space padded |
| `0x033` | 1 | HashAlgorithmId | `1` for FNV-1a-32 |
| `0x034` | 2 | CompanionRecordVersion | `1` |
| `0x036` | 2 | PrimaryRecordVersion | `1` |
| `0x038` | 4 | SuperblockCRC32 | CRC-32/ISO-HDLC of bytes `0x000..0x03F` with this field zeroed |
| `0x03C` | 4 | ReservedHeader | Must be zero |
| `0x040` | 446 | Reserved | Must be zero when formatted |
| `0x1FE` | 2 | TrailerSignature | `0xAA55` little-endian |

- **FR-053**: Mount validation MUST verify the magic, version, header size, sector size, cluster size, FAT count, reserved-sector count, entry sizes, hash algorithm, root cluster, volume capacity, FAT boundaries, CRC, and trailer signature before trusting derived offsets.
- **FR-054**: The backup superblock at sector 1 MUST contain the same version-1 fields as the primary superblock.
- **FR-055**: If the primary superblock is invalid but the backup is valid, the implementation MAY expose diagnostic read-only access but MUST NOT mount the volume writable.
- **FR-056**: If both superblocks are valid but differ in any required field, the implementation MUST report inconsistency and MUST NOT mount the volume writable.
- **FR-057**: Version-1 mount code MUST reject any nonzero reserved field whose semantics are undefined by version 1 when accepting it could alter interpretation.

#### Primary Directory Record

- **FR-058**: A primary file or directory record MUST occupy exactly 32 bytes.
- **FR-059**: The version-1 primary record MUST use the FAT32 short-directory-entry layout:

| Offset | Size | Field | Version-1 Use |
|---:|---:|---|---|
| `0x00` | 11 | Name | 8-byte base + 3-byte extension, uppercase ASCII and space padded |
| `0x0B` | 1 | Attributes | Directory or regular-file attributes |
| `0x0C` | 1 | Reserved | Zero |
| `0x0D` | 1 | CreateTenth | Zero in minimal release |
| `0x0E` | 2 | CreateTime | Zero in minimal release |
| `0x10` | 2 | CreateDate | Zero in minimal release |
| `0x12` | 2 | AccessDate | Zero in minimal release |
| `0x14` | 2 | FirstClusterHigh | Bits 16..31 of first cluster |
| `0x16` | 2 | WriteTime | Zero in minimal release |
| `0x18` | 2 | WriteDate | Zero in minimal release |
| `0x1A` | 2 | FirstClusterLow | Bits 0..15 of first cluster |
| `0x1C` | 4 | FileSize | Regular-file byte length; zero for directories |

- **FR-060**: Version 1 MUST use attribute `0x10` for directories and `0x20` for regular files; unsupported attribute combinations MUST be rejected or reported.
- **FR-061**: A first byte of `0x00` in a primary directory slot MUST mark the end of used directory records.
- **FR-062**: A first byte of `0xE5` MUST mark a deleted/reusable directory slot.
- **FR-063**: Regular-file primary entries MUST always be preceded immediately by their required extension-hash companion entry.
- **FR-064**: Directory primary entries MUST NOT require extension-hash companion entries in version 1.
- **FR-065**: An empty regular file MUST use first-cluster value 0 and file size 0 until data storage is allocated.
- **FR-066**: The first release MUST enforce a maximum regular-file size of `0xFFFFFFFF` bytes and MUST fail before an operation would exceed it.
- **FR-067**: Directory entries MUST be allocated from 32-byte slots within directory cluster chains.
- **FR-068**: A new regular file MUST have two consecutive available slots so its companion and primary records remain adjacent.
- **FR-069**: If no two consecutive slots are available, the directory implementation MAY extend the directory cluster chain even if isolated reusable slots remain elsewhere.
- **FR-070**: A companion-primary pair MUST NOT cross the logical end of a directory cluster; when only one slot remains, a new directory cluster MUST be used for the pair.

#### Filename and Extension Rules

- **FR-071**: The first release MUST support only short filenames and MUST NOT implement VFAT long-filename records.
- **FR-072**: A regular filename MUST contain a base name of 1 through 8 supported characters and MAY contain an extension of 0 through 3 supported characters.
- **FR-073**: A directory name MUST contain a base name of 1 through 8 supported characters and MUST NOT require an extension in version 1.
- **FR-074**: User-entered lowercase ASCII letters MUST be converted to uppercase before on-disk storage and filename comparison.
- **FR-075**: Version-1 supported filename characters MUST be uppercase `A-Z`, digits `0-9`, underscore `_`, and hyphen `-`.
- **FR-076**: The dot `.` MUST be treated as the user-visible separator between base name and extension and MUST NOT be stored in the 11-byte primary name field.
- **FR-077**: Unused base-name and extension bytes in the 11-byte primary name field MUST be padded with ASCII space `0x20`.
- **FR-078**: Filename lookup MUST be case-insensitive for supported ASCII input by applying the same uppercase canonicalization used for creation.
- **FR-079**: Names containing additional dots, unsupported characters, whitespace, control characters, path separators inside a component, an overlength base name, or an overlength extension MUST fail without silent truncation.
- **FR-080**: Duplicate canonical 8.3 names within one directory MUST be rejected.

#### Extension-Hash Companion Record

- **FR-081**: Every committed regular file MUST have exactly one extension-hash companion record immediately preceding its primary record.
- **FR-082**: The companion record MUST occupy exactly 32 bytes and MUST use the following version-1 layout:

| Offset | Size | Field | Version-1 Meaning |
|---:|---:|---|---|
| `0x00` | 1 | RecordType | `0xF1` |
| `0x01` | 1 | RecordVersion | `1` |
| `0x02` | 1 | HashAlgorithmId | `1` for FNV-1a-32 |
| `0x03` | 1 | Flags | Bit 0 = committed; bits 1..7 zero |
| `0x04` | 1 | ExtensionLength | Canonical extension length `0..3` |
| `0x05` | 1 | PrimaryNameChecksum | Checksum of following primary 11-byte name |
| `0x06` | 2 | Reserved0 | Zero |
| `0x08` | 4 | ExtensionHash | FNV-1a-32 of canonical extension, little-endian |
| `0x0C` | 4 | RecordCRC32 | CRC-32/ISO-HDLC of all 32 bytes with this field zeroed |
| `0x10` | 16 | Reserved1 | Zero in version 1 |

- **FR-083**: A companion record is recognized only when `RecordType`, version, algorithm identifier, flags, reserved fields, and CRC satisfy version-1 validation.
- **FR-084**: Version-1 companion flag bit 0 MUST be set for a committed record; uncommitted records MUST NOT make the following file visible as a healthy committed regular file.
- **FR-085**: The companion `PrimaryNameChecksum` MUST use the following eight-bit algorithm over the following primary entry's exact 11 stored name bytes: starting from zero, for each byte compute `sum = (((sum & 1) ? 0x80 : 0) + (sum >> 1) + byte) mod 256`.
- **FR-086**: A companion record whose `PrimaryNameChecksum` does not match the following primary entry MUST be treated as corrupt.
- **FR-087**: A companion record not followed immediately by a valid regular-file primary record MUST be treated as orphaned/corrupt.
- **FR-088**: A regular-file primary record not immediately preceded by a valid committed companion MUST be treated as incomplete/corrupt.
- **FR-089**: Deleted companion and primary records MUST each be marked reusable by setting their first byte to `0xE5`.
- **FR-090**: A companion-primary pair MUST be exposed as one VFS regular file and MUST NOT appear as two directory objects.

#### Extension Canonicalization and Hashing

- **FR-091**: The canonical extension MUST be derived from the three extension bytes of the primary 11-byte short name.
- **FR-092**: Canonicalization MUST remove only trailing ASCII-space padding from the three stored extension bytes.
- **FR-093**: Because primary names are stored uppercase, the canonical extension bytes MUST be uppercase ASCII.
- **FR-094**: The separator dot and base filename MUST NOT participate in the extension hash.
- **FR-095**: An empty extension MUST hash the zero-length byte sequence and MUST store `ExtensionLength = 0`.
- **FR-096**: Hash algorithm identifier 1 MUST mean FNV-1a-32 with offset basis `0x811C9DC5` and prime `0x01000193`.
- **FR-097**: FNV-1a-32 processing MUST begin with the offset basis and, for each canonical extension byte, compute `hash = (hash XOR byte) * prime mod 2^32`.
- **FR-098**: The resulting 32-bit hash MUST be stored little-endian in the companion record.
- **FR-099**: Hash collisions MUST NOT affect filename uniqueness, lookup correctness, rename correctness, or deletion correctness.
- **FR-100**: Any operation that uses the extension hash to prefilter or classify entries MUST verify the authoritative primary extension before returning an exact-match result.
- **FR-101**: Changing only the file's data or file size MUST NOT require recomputing the extension hash.
- **FR-102**: Changing the base name while retaining the same extension MUST preserve the extension hash but MUST update the primary-name association checksum and companion CRC.
- **FR-103**: Changing the extension MUST recompute the canonical extension, extension length, extension hash, primary-name checksum, and companion CRC.

#### CRC-32 Integrity

- **FR-104**: Superblock and companion-record integrity fields MUST use CRC-32/ISO-HDLC with reflected polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, and final XOR `0xFFFFFFFF`.
- **FR-105**: Before calculating a structure's CRC, the structure's own CRC field MUST be treated as zero.
- **FR-106**: A CRC mismatch MUST cause the affected structure to be reported invalid rather than silently accepted.

#### Directory and File Operations

- **FR-107**: The root directory MUST be stored as a normal cluster chain beginning at cluster 2.
- **FR-108**: Subdirectories MUST be stored as cluster chains and MUST contain `.` and `..` primary directory entries; these two internal entries are special filesystem records and are exempt from ordinary user-created filename character rules.
- **FR-109**: `.` MUST refer to the subdirectory's own first cluster and `..` MUST refer to its parent's first cluster; root-parent resolution MUST resolve to root.
- **FR-110**: Directory enumeration MUST safely skip deleted slots and MUST stop at a valid end-of-directory marker unless the directory chain ends first.
- **FR-111**: Directory scanning MUST understand primary directory records and InferenceFS-FAT32 hash companions but MUST reject unsupported record types rather than treating their bytes as ordinary filenames.
- **FR-112**: File creation MUST initialize a valid uncommitted companion record, create the following primary record, then make the companion committed only after the pair is internally consistent; changing the committed flag MUST be accompanied by recomputation of the companion CRC before the committed record is flushed.
- **FR-113**: The exact physical write order MUST ensure that an interrupted creation can be detected as incomplete on the next scan.
- **FR-114**: File deletion MUST make the file inaccessible as a committed directory entry and release its data clusters without freeing clusters belonging to another file.
- **FR-115**: Rename MUST fail when the destination canonical name already exists.
- **FR-116**: Rename across extensions MUST update both primary and companion metadata as one logical operation.
- **FR-117**: A failed rename MUST NOT report success unless the resulting primary and companion records validate together.
- **FR-118**: Reading and writing MUST validate file offsets, file size, cluster numbers, arithmetic boundaries, and block ranges.
- **FR-119**: File writes MUST allocate additional clusters when required and MUST correctly link them into the file chain.
- **FR-120**: Deleting or truncating data MUST free clusters exactly once.
- **FR-121**: Newly allocated file clusters MUST be zero-initialized before unwritten bytes can be read through the VFS.
- **FR-122**: Sparse files are not supported; seeking beyond end-of-file for a write MUST either zero-fill the intervening range or be rejected according to one documented first-release rule.
- **FR-123**: The first release MUST choose the simpler rule of rejecting a write whose starting offset is greater than the current file size.
- **FR-124**: Appending MUST write at exactly the current file size.

#### Mount, Validation, and Corruption Handling

- **FR-125**: Mount MUST validate superblock geometry before reading the FAT or data region.
- **FR-126**: Mount MUST validate FAT entries required to access the root directory before exposing the filesystem.
- **FR-127**: Writable mount MUST fail when the filesystem contains structural corruption that can make mutation unsafe.
- **FR-128**: Diagnostic read-only inspection MAY be provided for corrupt volumes when bounds can still be established safely.
- **FR-129**: FAT chain traversal MUST detect loops using a bounded method and MUST never traverse more clusters than exist in the volume.
- **FR-130**: FAT chain traversal MUST reject out-of-range, reserved, bad, or malformed next-cluster values.
- **FR-131**: A regular file whose size exceeds the capacity of its valid cluster chain MUST be reported corrupt.
- **FR-132**: A regular file whose chain contains more allocated bytes than its size MAY be accepted when the excess is only unused bytes in the final allocated cluster.
- **FR-133**: Automatic repair is not required in the first release.
- **FR-134**: The system MUST clearly distinguish clean mount, read-only diagnostic mount, and rejected mount states.

#### Persistence, Cache, and Flush

- **FR-135**: The kernel MUST provide a minimal block cache or equivalent buffering layer that tracks clean and dirty sectors.
- **FR-136**: Dirty filesystem metadata and data MUST be written to the block device before a flush operation reports success.
- **FR-137**: `sync` MUST request filesystem flush followed by block-device flush.
- **FR-138**: `unmount` MUST refuse or safely complete outstanding VFS operations, flush required data, and invalidate the mount.
- **FR-139**: `reboot` and `shutdown` MUST perform the same required filesystem and block-device flush sequence before restart or halt.
- **FR-140**: A failed block write or flush MUST be reported and MUST prevent the system from falsely reporting the affected operation as durable.
- **FR-141**: Successfully acknowledged and flushed file contents and directory metadata MUST survive an orderly reboot in the reference QEMU profile.

#### Diagnostics

- **FR-142**: `fsinfo` MUST display filesystem signature, version, total sectors, cluster size, FAT size, root cluster, total data clusters, free-cluster count, primary record size, companion record size, and hash algorithm.
- **FR-143**: `fileinfo <path>` MUST display canonical name, object type, attributes, size, first cluster, primary-record location, and companion-record location for regular files.
- **FR-144**: `hashinfo <path>` MUST display visible extension, canonical extension bytes, extension length, hash algorithm, stored extension hash, recomputed extension hash, record version, committed state, primary-name checksum status, companion CRC status, and overall validation result.
- **FR-145**: `fatinfo <path>` MUST display the file or directory cluster chain and safely identify the end-of-chain marker.
- **FR-146**: `diskinfo` MUST display block-device identity, logical sector size, total sectors, capacity, and current status.
- **FR-147**: Diagnostic output MUST NOT disclose unrelated kernel-memory contents or read outside validated filesystem bounds.

#### C17 Freestanding Implementation and Toolchain Constraints

- **FR-148**: All project-owned kernel, VFS, filesystem, command, block, and support code written in C MUST conform to ISO C17 except for extensions explicitly allowed by this specification.
- **FR-149**: All kernel C translation units MUST be compiled in a freestanding environment with the language mode explicitly set to C17.
- **FR-150**: Project-owned kernel code MUST NOT depend on a host operating-system C library or host system-call interface.
- **FR-151**: The project MUST provide the freestanding memory/string runtime functions required by its code and by supported compiler-generated calls, including at minimum `memcpy`, `memmove`, `memset`, and `memcmp`.
- **FR-152**: The supported compiler families MUST be GCC and Clang targeting x86-64 ELF/freestanding output.
- **FR-153**: The primary build MAY use GCC, but a Clang validation build MUST compile all project-owned C translation units.
- **FR-154**: Compiler-specific extensions MUST be documented in a source-controlled extension allowlist.
- **FR-155**: The initial extension allowlist MAY include only narrowly scoped equivalents of: packed-layout annotation, explicit alignment, section placement, no-return annotation, used/retain annotation where needed by linking, compiler barriers, and inline assembly inside the architecture abstraction.
- **FR-156**: Compiler-specific annotations MUST be wrapped behind project macros or architecture/compiler abstraction headers rather than scattered as raw GCC/Clang syntax through generic code.
- **FR-157**: GNU nested functions, statement expressions, `typeof`-dependent generic logic, computed goto, and zero-length arrays MUST NOT be used in generic project code.
- **FR-158**: Variable-length arrays MUST NOT be used in kernel code.
- **FR-159**: Inline assembly MUST be confined to x86-64 architecture headers or source files and MUST have documented inputs, outputs, clobbers, and memory-order implications.
- **FR-160**: Standalone x86-64 assembly MUST be limited to boot/kernel entry transitions, descriptor-table or interrupt/exception stubs when required, CPU control-register operations that cannot be expressed through approved C wrappers, and similarly unavoidable architecture mechanisms.
- **FR-161**: VFS behavior, InferenceFS-FAT32 allocation logic, filename parsing, hashing, CRC calculation, directory parsing, and command semantics MUST NOT be implemented in assembly.
- **FR-162**: On-disk structures MUST have build-time `_Static_assert` checks for exact size and required field offsets.
- **FR-163**: Code that reads or writes on-disk structures MUST use explicit little-endian access rules and MUST NOT rely on unspecified compiler padding, host pointer layout, or host library serialization.
- **FR-164**: The build MUST fail when an on-disk structure size or required offset differs from the specification.
- **FR-165**: Warnings selected by the project as correctness gates MUST be treated as build failures in continuous integration.
- **FR-166**: The exact supported GCC, Clang, assembler, linker, QEMU, and UEFI firmware versions or version ranges MUST be recorded by the implementation plan and release manifest without changing this behavioral specification.

#### Testing and Reproducibility

- **FR-167**: Host-side unit tests MUST cover filename parsing, uppercase canonicalization, name padding, extension extraction, FNV-1a-32, CRC-32, primary-name checksum, FAT geometry, FAT-chain validation, companion validation, and path parsing.
- **FR-168**: Image-level tests MUST cover format, mount, create, write, append, read, rename, delete, directory creation, directory navigation, sync, reboot persistence, and unmount.
- **FR-169**: Tests MUST cover empty, one-character, two-character, and three-character extensions.
- **FR-170**: Tests MUST cover multiple files sharing one extension and files using different extensions.
- **FR-171**: Hash-collision safety MUST be tested using a deterministic test seam or crafted metadata; the production test suite MUST NOT depend on discovering a natural FNV-1a collision within the supported three-character extension space.
- **FR-172**: Corruption tests MUST cover missing companion, orphaned companion, duplicate companion, bad CRC, unsupported companion version, unsupported hash algorithm, mismatched name checksum, mismatched extension hash, invalid FAT entry, looped chain, out-of-range cluster, and inconsistent superblocks.
- **FR-173**: Fault-injection or interrupted-write tests MUST cover at least companion creation, primary-entry creation, extension-changing rename, file deletion, FAT extension, and block flush.
- **FR-174**: A clean checkout MUST have one documented build workflow that produces the boot image, reference data disk, symbols, and launch instructions.
- **FR-175**: The project MUST publish enough format documentation for an independent developer to parse a version-1 InferenceFS-FAT32 image without reading kernel source.

### Key Entities

- **InferenceFS-FAT32 Volume**: A versioned persistent block-backed filesystem containing a primary superblock, backup superblock, one FAT, and a cluster-addressed data region.
- **Superblock**: The 512-byte structure identifying the volume and defining version, geometry, record sizes, root cluster, and hash algorithm.
- **FAT Entry**: A 32-bit allocation record whose lower 28 bits represent free, bad, next-cluster, or end-of-chain state.
- **Cluster**: A fixed 4096-byte allocation unit containing file data or directory records.
- **Directory**: A cluster chain containing 32-byte slots interpreted as primary records, extension-hash companions, deleted slots, or end markers.
- **Primary Directory Record**: A 32-byte FAT32-derived 8.3 record containing the authoritative visible short name, attributes, first cluster, and file size.
- **Extension-Hash Companion Record**: A 32-byte InferenceFS-FAT32 record immediately preceding a regular-file primary record and containing the canonical extension hash plus association and integrity metadata.
- **Regular File Entry Set**: Exactly two adjacent directory slots: one committed hash companion followed by one primary regular-file record.
- **Canonical Extension**: Zero to three uppercase ASCII bytes derived from the primary short-name extension after removing trailing space padding.
- **VFS Mount**: The generic filesystem attachment that exposes the InferenceFS-FAT32 root at `/`.
- **VFS File Reference**: An opaque kernel-side object used by generic command code to access a file without knowing its raw filesystem-sector layout.
- **Block Device**: The generic representation of the persistent QEMU ATA disk.
- **Block Cache Entry**: A cached logical sector with clean/dirty state and block-device identity.
- **Command Context**: The current working directory, command input buffer, and VFS context used by the character prompt.

---

## Success Criteria (mandatory)

### Measurable Outcomes

- **SC-001**: On the documented reference host profile, at least 20 consecutive clean boots reach the `InferenceOS>` prompt without manual debugger intervention.
- **SC-002**: The complete mandatory demonstration sequence—format, mount, create `TEST.TXT`, write, inspect hash metadata, rename to `TEST.LOG`, sync, reboot, remount, verify, delete, and shutdown—passes end-to-end.
- **SC-003**: After 20 orderly sync-and-reboot cycles containing file creation and modification, every acknowledged test file has byte-for-byte expected content and a valid companion record after remount.
- **SC-004**: 100% of supported filename canonicalization tests produce the same stored 8.3 name and extension hash under the GCC and Clang validation builds.
- **SC-005**: For all test inputs containing extensions of length 0 through 3, `hashinfo` reports a stored FNV-1a-32 value equal to independently computed expected vectors.
- **SC-006**: Every injected missing, orphaned, unsupported, bad-CRC, mismatched-name-checksum, and mismatched-extension-hash companion case is detected and is never presented as a healthy committed regular file.
- **SC-007**: Every injected FAT loop, out-of-range cluster, bad-cluster misuse, invalid superblock, and impossible geometry case is detected without an out-of-volume block access or uncontrolled traversal.
- **SC-008**: A forced hash collision in the test harness does not cause two files with different authoritative extensions to compare equal, overwrite one another, or become indistinguishable.
- **SC-009**: Disk-full and injected block-write failures return defined failures while preserving all file contents previously acknowledged as durable.
- **SC-010**: A version-1 directory cluster can be parsed deterministically into primary records, companion records, deleted slots, and end markers with no ambiguous record classification under the supported filename character set.
- **SC-011**: Build-time assertions confirm every specified superblock, primary-record, and companion-record size and field offset in both supported compiler profiles.
- **SC-012**: All project-owned C translation units compile under the declared GCC and Clang freestanding C17 profiles without use of an extension outside the approved allowlist.
- **SC-013**: No filesystem, VFS, hashing, CRC, filename, or command-policy behavior is implemented in x86-64 assembly.
- **SC-014**: An external evaluator can build the project from a clean checkout, launch the reference QEMU profile, and complete the documented filesystem demonstration using only repository documentation.
- **SC-015**: `fileinfo`, `hashinfo`, and `fatinfo` expose enough information to correlate a visible regular file with its primary record, companion record, stored extension hash, first cluster, and cluster chain without a host-side disk editor.

---

## Assumptions

- The attached constitution is the governing authority. In particular, the project remains a minimal filesystem demonstrator, requires a VFS boundary, requires InferenceFS-FAT32, and requires a distinct 32-byte companion record containing the extension hash.
- The first release intentionally supports only x86-64 UEFI under QEMU and one virtual CPU.
- A separate UEFI-readable boot medium is available; the custom root/data filesystem does not need to be firmware-readable.
- The first release uses a raw whole-disk InferenceFS-FAT32 data image rather than implementing GPT or MBR parsing.
- QEMU's PC/i440fx-compatible IDE path is selected because a polling ATA PIO driver is sufficient for the minimal demonstration and avoids requiring a more complex storage stack.
- The reference data disk is 64 MiB, while formatting is supported for 16 MiB through 1 GiB disks using 512-byte sectors.
- Version 1 uses fixed 4 KiB clusters and one FAT.
- Version 1 uses short 8.3 names only. Long filenames are intentionally deferred.
- Supported user filename characters are deliberately narrower than traditional FAT short-name character sets to keep canonicalization deterministic and ASCII-only.
- Directories do not carry extension-hash companion records in version 1; the mandatory companion applies to regular files.
- FNV-1a-32 is selected because the extension hash is a small deterministic classification/lookup metadata value, not a cryptographic security primitive.
- CRC-32/ISO-HDLC is used only for on-disk integrity detection and is not treated as a security primitive.
- Exact extension text in the primary record remains authoritative even if hash collisions occur.
- Timestamps are represented by reserved FAT32-derived fields but remain zero in the minimal release; a real-time clock is not required.
- The command prompt runs in the kernel or an equivalently privileged single execution context; user-mode processes and process isolation are deferred.
- The command interface primarily manipulates ASCII text for demonstration, while VFS read/write operations themselves operate on bytes.
- The first release has no permissions, authentication, ACLs, networking, dynamic linking, package management, or graphical interface.
- Automatic filesystem repair is out of scope; detection and safe refusal/read-only diagnostics are sufficient.
- The exact open-source license will be selected before the first public release and does not change the feature's behavioral acceptance criteria.
- The implementation plan will lock exact compiler, assembler, linker, QEMU, and UEFI firmware versions or supported ranges.

---

## Out of Scope

The following are explicitly outside this feature specification:

- ARM or other CPU architectures.
- BIOS boot.
- Physical-hardware support.
- Hypervisors other than the documented QEMU reference profile.
- More than one active CPU.
- General user-mode application execution.
- ELF application loading as an operating-system feature.
- `fork`, `exec`, multitasking, scheduling, or process isolation.
- Multiple users, authentication, permissions, ACLs, or security domains.
- Networking, sockets, USB, audio, graphics, mouse, or touch.
- POSIX compatibility.
- Dynamic linking and shared libraries.
- Package management.
- VFAT or other long-filename support.
- Standard FAT32 interoperability for the InferenceFS-FAT32 volume.
- Partition-table parsing for the root data disk.
- Multiple mounted persistent filesystems.
- Journaling.
- Snapshots.
- Encryption or compression.
- Hard links or symbolic links.
- Sparse files.
- Extended attributes.
- Quotas.
- Memory-mapped files.
- Automatic recovery or repair of every corruption class.
- Cryptographic trust based on the extension hash or CRC.
- Treating the extension hash as proof of actual file content or file type.

---

## Constitution Check

This specification satisfies the governing constitution as follows:

| Constitutional Rule | Specification Compliance |
|---|---|
| Minimal Demonstration Scope | User stories are limited to boot, prompt, VFS, InferenceFS-FAT32, persistence, diagnostics, and reproducibility. |
| Character-Prompt Operating Environment | The reference target is x86-64 UEFI QEMU with a kernel-resident character command prompt. |
| Mandatory VFS Boundary | All ordinary file operations flow through the VFS; raw filesystem diagnostics are isolated. |
| InferenceFS-FAT32 Allocation Model | The volume uses a distinct identity with FAT-style 32-bit cluster allocation and cluster chains. |
| Extension-Hash Directory Entry | Every regular file uses a separate 32-byte companion immediately before its 32-byte primary record. |
| Hash is Derived Metadata | The primary extension remains authoritative; collisions cannot determine identity or equality. |
| Persistent Filesystem Integrity | Creation, rename, delete, validation, flush, reboot, CRC, and corruption behavior are specified and testable. |
| Observable Filesystem Demonstration | `fsinfo`, `fileinfo`, `hashinfo`, and `fatinfo` expose the experiment from the command prompt. |
| Simplicity and Reproducibility | Single CPU, polling-capable devices, one mount, raw disk, short names, no process model, and one documented build path keep the implementation bounded. |
| C17 Freestanding Constraint | Project-owned C is ISO C17 freestanding with an explicit GCC/Clang extension allowlist and small isolated x86-64 assembly. |

No requirement in this specification replaces the VFS with direct command-to-filesystem access, removes the companion record, makes a hash authoritative over extension text, or expands the project into a general-purpose operating system.

---

## Specification Readiness

The technical plan MUST preserve all constitution-bound decisions and MUST specifically map:

1. boot and kernel initialization;
2. console and keyboard;
3. generic block-device and ATA PIO driver;
4. block cache;
5. VFS contracts;
6. InferenceFS-FAT32 formatter and mount validator;
7. FAT allocation and chain traversal;
8. directory record parsing and mutation;
9. FNV-1a extension hashing;
10. CRC-32 integrity checks;
11. file and directory operations;
12. command implementation;
13. host-side unit tests;
14. QEMU integration tests; and
15. GCC/Clang C17 freestanding build profiles
