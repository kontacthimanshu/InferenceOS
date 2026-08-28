# InferenceOS-FS Version 1

InferenceOS-FS is the persistent root filesystem used by the InferenceOS demonstrator. It borrows
FAT-style allocation and 8.3 directory names, but it has its own signature, superblock, companion
records, registry region, validation rules, and recovery behavior. It is not FAT32-compatible and
must not be presented as such. The firmware-readable EFI System Partition is a separate volume.

This document describes the fixed version-1 disk format implemented under
`src/filesystems/inferenceos_fs/`. Multi-byte integers are little-endian. Byte-array fields and
compile-time size/offset assertions keep the layout independent of compiler padding and host byte
order.

## Format limits and constants

| Property | Version-1 value |
|---|---:|
| Magic | ASCII `INFOSFS1` |
| Minimum volume capacity | 50,000,000,000 bytes |
| Logical sector size | 512 bytes |
| Sectors per cluster | 8 |
| Cluster size | 4096 bytes |
| Superblock copies | 2, at sectors 0 and 1 |
| FAT copies | 1 |
| Extension Registry size | 4096 sectors (2 MiB) |
| Root directory cluster | 2 |
| Directory slot size | 32 bytes |
| Slots per cluster | 128 |
| Maximum regular-file size | `0xFFFFFFFF` bytes |
| Sparse regular files | Not supported |

A device with a logical sector size other than 512 bytes, or a capacity below the minimum, is
rejected. All sector, cluster, FAT, file-offset, and capacity calculations are checked for overflow
and range validity before I/O.

## Volume geometry

Let `F` be the fixed-point FAT length in sectors. Version 1 lays out the volume as follows:

| Sector range | Contents |
|---|---|
| `0` | Primary 512-byte superblock |
| `1` | Backup 512-byte superblock |
| `2 .. 2 + F - 1` | One FAT, with 32-bit entries |
| `2 + F .. 2 + F + 4095` | Reserved Extension Registry |
| `2 + F + 4096 .. end` | Cluster-addressed directory and file data |

The formatter chooses the smallest `F` for which the FAT contains at least `cluster_count + 2`
entries. The data region contains only complete eight-sector clusters; any final partial cluster is
not addressable. Cluster 2 is the first data cluster and owns the root directory. For a validated
cluster `C`, its first sector is:

```text
data_start_sector + (C - 2) * 8
```

These reference vectors are enforced by the geometry tests:

| Profile | Total sectors | FAT sectors | Registry start | Data start | Clusters | Usable bytes |
|---|---:|---:|---:|---:|---:|---:|
| 50,000,000,000 bytes | 97,656,250 | 95,271 | 95,273 | 99,369 | 12,194,610 | 49,949,122,560 |
| 64 GiB | 134,217,728 | 130,941 | 130,943 | 135,039 | 16,760,336 | 68,650,336,256 |

Formatting zeroes the FAT, registry, and initial root-directory cluster; marks FAT entries 0, 1,
and root cluster 2 as end-of-chain; writes the backup superblock before the primary; and completes a
device flush before reporting success.

## Superblock

Each superblock occupies one sector. Structural fields in the two copies must match byte-for-byte
for a read-write mount.

| Offset | Size | Field | Version-1 meaning |
|---|---:|---|---|
| `0x000` | 8 | Magic | ASCII `INFOSFS1` |
| `0x008` | 2 | FormatVersion | `1` |
| `0x00A` | 2 | HeaderSize | `80` |
| `0x00C` | 2 | BytesPerSector | `512` |
| `0x00E` | 1 | SectorsPerCluster | `8` |
| `0x00F` | 1 | FatCount | `1` |
| `0x010` | 2 | ReservedSuperblockSectors | `2` |
| `0x012` | 2 | PrimaryDirectoryRecordSize | `32` |
| `0x014` | 2 | CompanionRecordSize | `32` |
| `0x016` | 2 | Flags | Must be zero |
| `0x018` | 8 | TotalSectors | Device logical-sector count |
| `0x020` | 4 | SectorsPerFat | Fixed-point FAT length |
| `0x024` | 4 | RootCluster | `2` |
| `0x028` | 8 | RegistryStartSector | `2 + SectorsPerFat` |
| `0x030` | 4 | RegistrySectorCount | `4096` |
| `0x034` | 4 | VolumeSerial | Non-security identifier |
| `0x038` | 11 | VolumeLabel | Uppercase ASCII/digits/underscore, space padded |
| `0x043` | 1 | HashAlgorithmId | `1`, FNV-1a-32 |
| `0x044` | 2 | CompanionRecordVersion | `1` |
| `0x046` | 2 | RegistryRecordVersion | `1` |
| `0x048` | 4 | SuperblockCRC32 | CRC-32/ISO-HDLC with this field zeroed |
| `0x04C` | 4 | ReservedHeader | Must be zero |
| `0x050` | 430 | Reserved | Must be zero |
| `0x1FE` | 2 | TrailerSignature | Little-endian `0xAA55` |

The decoder validates the signature, versions, fixed sizes, flags, geometry, registry placement,
volume label, hash identifier, CRC, reserved bytes, and trailer before derived offsets are trusted.
An unknown or structurally incompatible version is not accepted as version 1.

## FAT and cluster chains

Each FAT entry is a 32-bit little-endian value. The upper four bits must be zero; the lower 28 bits
have these meanings:

| Value | Meaning |
|---|---|
| `0x00000000` | Free cluster |
| Valid data-cluster number | Next cluster in the chain |
| `0x0FFFFFF0 .. 0x0FFFFFF6` | Reserved and invalid in a healthy chain |
| `0x0FFFFFF7` | Bad cluster |
| `0x0FFFFFF8 .. 0x0FFFFFFF` | End-of-chain |

Entries 0 and 1 are reserved. Data clusters start at 2 and may not exceed `0x0FFFFFEF` in version
1. Traversal validates every value before calculating an offset, is bounded by the volume's cluster
count, detects loops, and rejects free, bad, reserved, malformed, or out-of-range links. Ownership
validation also rejects a cluster claimed by more than one file or directory. A regular file is
corrupt when its validated chain cannot supply its declared size; unused bytes in the final cluster
are permitted.

## Names and primary directory records

Version 1 stores internal names as uppercase, space-padded 8.3 names. The supported characters are
`A-Z`, `0-9`, `_`, and `-`. Lowercase input is converted to uppercase. The dot is a parser separator
and is not stored. Empty base names, multiple dots, path separators, unsupported characters, base
names longer than eight bytes, and extensions longer than three bytes fail without truncation.
Canonical 11-byte names must be unique within a directory.

Every primary record is 32 bytes:

| Offset | Size | Field | Version-1 use |
|---|---:|---|---|
| `0x00` | 11 | Name | Eight-byte base plus three-byte extension, space padded |
| `0x0B` | 1 | Attributes | Directory `0x10` or regular file `0x20` |
| `0x0C` | 1 | Reserved | Zero |
| `0x0D` | 1 | CreateTenth | Zero |
| `0x0E` | 2 | CreateTime | Zero |
| `0x10` | 2 | CreateDate | Zero |
| `0x12` | 2 | AccessDate | Zero |
| `0x14` | 2 | FirstClusterHigh | Cluster bits 16..31 |
| `0x16` | 2 | WriteTime | Zero |
| `0x18` | 2 | WriteDate | Zero |
| `0x1A` | 2 | FirstClusterLow | Cluster bits 0..15 |
| `0x1C` | 4 | FileSize | Regular-file size; zero for directories |

A first byte of `0x00` ends the used directory records. A first byte of `0xE5` marks a deleted and
reusable slot. Empty regular files have first cluster 0 and size 0. Directories have a single
primary record, no companion, size 0, and a cluster chain containing internal `.` and `..` entries.

Regular files consume two consecutive slots: companion first, primary second. The pair cannot cross
a directory-cluster boundary; if only the last slot is free, allocation continues in an extended
directory cluster. Enumeration exposes a validated pair as one file and never exposes companions as
independent objects.

Ordinary directory enumeration follows each validated directory's FAT chain and converts healthy
records into VFS entries. Directories carry folder identity; regular files carry a kernel-private
type identity derived from their authoritative extension plus the companion hash as an internal
prefilter. These fields support GUI icon selection, but raw FAT entries, extensions, companion
records, and hashes never enter the ordinary File Explorer model.

## Extension-hash companion

Every healthy committed regular file has exactly one 32-byte companion immediately before its
primary record:

| Offset | Size | Field | Version-1 meaning |
|---|---:|---|---|
| `0x00` | 1 | RecordType | `0xF1` |
| `0x01` | 1 | RecordVersion | `1` |
| `0x02` | 1 | HashAlgorithmId | `1`, FNV-1a-32 |
| `0x03` | 1 | Flags | Bit 0 committed; bits 1..7 zero |
| `0x04` | 1 | ExtensionLength | `0..3` |
| `0x05` | 1 | PrimaryNameChecksum | Association with the following primary name |
| `0x06` | 2 | Reserved0 | Zero |
| `0x08` | 8 | ExtensionHashText | Uppercase hexadecimal ASCII |
| `0x10` | 4 | RecordCRC32 | CRC-32/ISO-HDLC with this field zeroed |
| `0x14` | 12 | Reserved1 | Zero |

Pair validation requires all of the following:

- the companion type, version, algorithm, flags, reserved bytes, hash text, and CRC are valid;
- the companion is committed and immediately precedes a valid regular-file primary;
- `ExtensionLength` equals the authoritative primary extension length;
- `PrimaryNameChecksum` matches the following primary's exact 11 stored name bytes; and
- the stored hash text equals a fresh hash of the authoritative primary extension.

A missing, orphaned, duplicate, uncommitted, unsupported, bad-CRC, association-mismatched, or
hash-mismatched companion prevents the entry from being exposed as a healthy committed file.

## Extension canonicalization and hashing

The primary record is always authoritative. The hash is derived metadata and is not a filename,
content-type proof, security credential, unique identifier, or substitute for exact extension
comparison.

Canonicalization takes bytes 8 through 10 of the primary's 11-byte name, removes trailing ASCII
space padding only, and keeps the remaining uppercase bytes. The base name and separator dot never
participate. An extensionless file hashes the zero-length byte sequence and stores length 0.

Algorithm identifier 1 is FNV-1a-32:

```text
hash = 0x811C9DC5
for each canonical extension byte:
    hash = hash XOR byte
    hash = (hash * 0x01000193) modulo 2^32
```

The result is encoded as exactly eight uppercase hexadecimal characters. Tested vectors include:

| Canonical extension | Numeric hash | Stored text |
|---|---:|---|
| empty | `0x811C9DC5` | `811C9DC5` |
| `T` | `0xD10C0B43` | `D10C0B43` |
| `TX` | `0x30F57B81` | `30F57B81` |
| `TXT` | `0xE771F04F` | `E771F04F` |
| `BIN` | `0xDF81ECDE` | `DF81ECDE` |

For `REPORT.TXT`, the stored name is `REPORT  TXT`, the canonical extension is `TXT`, the
association checksum is `0xA3`, and the stored hash text is `E771F04F`.

The association checksum uses the FAT/VFAT rotate-right-and-add 8-bit algorithm over the exact 11
stored name bytes. Consequently, a base-name-only rename preserves the extension hash but still
updates the checksum and companion CRC. An extension-changing rename recomputes the length, hash,
checksum, and CRC. Hash-based lookup is only a prefilter: exact authoritative extension bytes and
length must match before a type match is returned, so collisions do not affect identity or routing.

### Extension search

The shell-facing `search <extension>` service accepts one to three supported extension characters,
with an optional leading dot, and canonicalizes ASCII case. The VFS delegates a read-only recursive
search to InferenceOS-FS. For every regular-file candidate, the filesystem validates the committed
primary/companion pair, compares the companion hash with the internally computed query hash as a
prefilter, and then compares the authoritative primary extension bytes and length exactly.

The reply contains at most 16 absolute, extension-hidden display paths plus an explicit truncation
flag. It contains no extension, stored or computed hash, companion, or raw directory record.
Traversal is bounded to 16 directory levels and 255-byte paths. Search never writes metadata and
does not consult the optional Extension Registry, so disabled, stale, corrupt, or full registry
state cannot change its results.

## CRC integrity

Superblocks, companions, and registry records use CRC-32/ISO-HDLC with reflected polynomial
`0xEDB88320`, initial value `0xFFFFFFFF`, and final XOR `0xFFFFFFFF`. A structure's own CRC field is
treated as zero while calculating its checksum. CRC is corruption detection, not cryptographic
authentication; a mismatch invalidates the structure.

## Extension Registry

The registry region is reserved in every version-1 volume, but use is disabled by default. It is a
derived, rebuildable optimization and never controls file identity, pair health, or correctness.
Each 32-byte record has this layout:

| Offset | Size | Field | Version-1 meaning |
|---|---:|---|---|
| `0x00` | 1 | RecordType | `0xE1` |
| `0x01` | 1 | RecordVersion | `1` |
| `0x02` | 1 | Flags | Bit 0 active |
| `0x03` | 1 | ExtensionLength | `0..3` |
| `0x04` | 1 | HashAlgorithmId | `1`, FNV-1a-32 |
| `0x05` | 3 | Reserved0 | Zero |
| `0x08` | 3 | CanonicalExtension | Uppercase ASCII, space padded |
| `0x0B` | 1 | Reserved1 | Zero |
| `0x0C` | 8 | ExtensionHashText | Eight uppercase hexadecimal characters |
| `0x14` | 4 | LastDirectoryCluster | Most recently associated primary's directory |
| `0x18` | 2 | LastDirectorySlot | Most recently associated primary slot |
| `0x1A` | 2 | UpdateGeneration | Wrapping, non-security generation |
| `0x1C` | 4 | RecordCRC32 | CRC-32/ISO-HDLC |

There may be at most one active logical record per canonical extension. A later committed file of
the same type refreshes that record instead of adding a duplicate. Lookups still scan and validate
authoritative companion-primary pairs; a registry location is only a hint. Disabled, stale, full,
corrupt, or rebuilding registry states fall back to authoritative scanning, and the registry can be
rebuilt from committed pairs. Registry failure alone does not change the filesystem mount state.
No performance benefit or default enablement is claimed without the matched research gate described
in `specs/001-inferenceos/research.md`.

## Mutation ordering and interruption states

An uncommitted companion is the visibility fence. Barriers write every dirty cache sector through
the current generation and complete the block-device flush before the next dependency group.

| Operation | Persisted order |
|---|---|
| Create/save | Uncommitted companion -> barrier -> content -> barrier -> FAT/allocation -> barrier -> primary -> committed companion -> barrier |
| Rename | Uncommitted companion -> barrier -> renamed primary -> recomputed committed companion -> barrier |
| Delete | Uncommitted companion -> barrier -> mark both records deleted -> barrier -> release owned allocation -> barrier |

The optional registry is refreshed only after the authoritative pair commits. A registry refresh
failure cannot make an authoritative commit healthy or unhealthy and cannot replace directory
scanning.

If any write or flush fails, the operation returns failure and must not report durable success. A
crash can leave the previous valid state or a detectable incomplete state, including an
uncommitted/missing pair or allocated but unreachable storage. Version 1 has no journal,
transactional rollback, or automatic repair and therefore does not claim that every interrupted
operation recovers space or restores the prior namespace automatically.

`sync` tracks content, allocation, primary, companion, and registry dirty components by cache
generation. Durable success requires the generation barrier and device flush to succeed. `unmount`
drains or refuses outstanding operations, performs the required synchronization, and invalidates
the mount/cache; reboot and shutdown likewise require successful synchronization.

## Mount and recovery classification

Mount validates device geometry and superblocks before reading the FAT, registry, directories, or
data. It then validates reserved FAT entries and the root chain with bounded loop detection.

| Mount state | Conditions | Permitted behavior |
|---|---|---|
| `IOS_MOUNT_RW` | Both superblocks validate and match exactly; geometry and root chain are safe | Normal reads and mutations |
| `IOS_MOUNT_DIAGNOSTIC` | Bounds and root traversal are safe, but exactly one superblock is invalid or both valid copies disagree | Explicit bounded diagnostic reads only; no mutation |
| `IOS_MOUNT_REJECTED` | Sector size/capacity is unsupported, both superblocks are unusable, geometry is inconsistent, root traversal is unsafe, or required I/O fails | No mount or filesystem access |

Diagnostic reports identify the reason as `device_geometry`, `superblocks_invalid`,
`primary_invalid`, `backup_invalid`, `superblock_disagreement`, `root_chain_unsafe`, or `device_io`,
and record whether neither, one, or both superblocks supplied trusted bounds.

Pair, FAT, ownership, and file-capacity validation remains range bounded after mount. Malformed
records are never reinterpreted as ordinary filenames, and invalid pairs are never reported as
healthy files. Diagnostic access is an explicit privileged interface and cannot read outside the
validated volume. Ordinary CUI, GUI, Shell, and application views use the VFS and hide extensions,
hashes, raw records, registry contents, and cluster locations; this is an interface contract, not
encryption against diagnostics or raw-disk examination.

## Compatibility and implementation references

Version 1 deliberately excludes long filenames, sparse files, multiple FATs, journaling, snapshots,
compression, encryption, and automatic repair. Any incompatible on-disk change requires a new
format version and specification/constitution review rather than silently reinterpreting version-1
reserved fields.

Authoritative implementation and validation anchors:

- [`format.h`](../src/filesystems/inferenceos_fs/include/inferenceos/fs/format.h) and
  [`superblock.c`](../src/filesystems/inferenceos_fs/superblock.c) define geometry and superblocks.
- [`records.h`](../src/filesystems/inferenceos_fs/include/inferenceos/fs/records.h) and
  [`records.c`](../src/filesystems/inferenceos_fs/records.c) define primary/companion records.
- [`transaction.c`](../src/filesystems/inferenceos_fs/transaction.c) and
  [`sync.c`](../src/filesystems/inferenceos_fs/sync.c) define persistence ordering.
- [`mount.c`](../src/filesystems/inferenceos_fs/mount.c),
  [`diagnostic_mount.c`](../src/filesystems/inferenceos_fs/diagnostic_mount.c), and
  [`validator.c`](../src/filesystems/inferenceos_fs/validator.c) define recovery classification.
- [`registry.h`](../src/filesystems/inferenceos_fs/include/inferenceos/fs/registry.h) and
  [`registry.c`](../src/filesystems/inferenceos_fs/registry.c) define the optional registry.
- The normative feature requirements and concise boundary contract are
  [`spec.md`](../specs/001-inferenceos/spec.md) and
  [`contracts/inferenceos-fs.md`](../specs/001-inferenceos/contracts/inferenceos-fs.md).
