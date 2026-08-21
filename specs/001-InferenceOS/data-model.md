# Data Model: InferenceOS Minimal

Exact on-disk offsets and constants remain authoritative in `spec.md` FR-035–FR-106.

## Persistent Entities

### InferenceFS-FAT32 Volume

- **Fields**: device identity/capacity, two superblocks, one FAT, data region, root cluster.
- **Validation**: 512-byte sectors; 16 MiB–1 GiB; checked, non-overlapping geometry; every derived LBA in bounds.
- **States**: unformatted → formatted/unmounted → clean writable or diagnostic read-only; unsafe inconsistency → rejected.

### Superblock

- **Fields**: `INFFAT32`, v1 geometry/record sizes, root, label/serial, algorithms/versions, CRC, trailer.
- **Validation**: exact 512-byte decoded layout, CRC-32/ISO-HDLC, zero reserved fields, primary/backup agreement.
- **Rule**: backup-only validity permits optional diagnostics, never writable recovery; disagreement rejects writable mount.

### FAT Entry and Cluster Chain

- **Fields**: 32-bit little-endian entry; lower 28 bits encode free, bad, next, reserved, or EOC; upper nibble zero.
- **Validation**: cluster and corresponding LBA checked before access; traversal never exceeds total data clusters.
- **State**: free → allocated/EOC → linked; linked → free only after the owning object is inaccessible.

### Directory Slot

- **Shape**: exactly 32 bytes in a directory cluster.
- **Kinds**: end, deleted/reusable, companion, regular primary, directory primary, unsupported, corrupt.
- **Validation**: type before interpretation; bounded scan; stop at a valid end marker.

### Primary Directory Record

- **Fields**: 11-byte uppercase space-padded name, attributes, first cluster, file size, zero v1 time/reserved fields.
- **Validation**: exact offsets, supported attribute/name, cluster/size consistency.
- **Relationship**: regular primaries require a preceding companion; directory primaries do not.

### Extension-Hash Companion

- **Fields**: type `0xF1`, version 1, algorithm 1, committed flag, extension length, name checksum, FNV-1a-32, CRC-32, zero reserved bytes.
- **Validation**: supported identifiers, flags/reserved bytes, checksum of following primary, CRC, and recomputed canonical hash.
- **States**: reusable → staged/uncommitted → committed valid → deleted. Invalid states never expose a healthy file.

### Regular File Entry Set

- **Shape**: companion plus regular primary, adjacent and ordered within one directory cluster.
- **Identity**: parent directory and full canonical 8.3 primary name, never the hash.
- **Transitions**:
  - Create: reserve pair → uncommitted companion → primary → flush → committed companion/CRC → visible.
  - Rename: hide pair → update primary → recompute checksum/hash as needed and CRC → recommit.
  - Delete: hide and flush → delete both slots → free the fully validated owned chain.

### Directory

- **Fields**: first cluster, chain, typed slots; subdirectories contain `.` and `..`.
- **Rules**: canonical names unique; at least 16 levels; parent cannot escape root; regular pairs cannot cross a cluster.

## Derived Values

### Canonical Short Name and Extension

- Base length 1–8; regular extension length 0–3; ASCII letters/digits/`_`/`-`; uppercase storage.
- Dot is a separator only; invalid or overlong input fails without truncation.
- Canonical extension is the primary record extension with trailing spaces removed.

### Extension Hash

- FNV-1a-32 algorithm ID 1 over zero to three uppercase extension bytes; little-endian storage.
- Used only as a prefilter/classification value; exact operations verify the authoritative extension and complete name.

## Runtime Entities

### Block Device and Cache Entry

- Device: opaque context plus read/write/flush/query and stable status.
- Cache entry: device, LBA, 512-byte data, valid/dirty/error state, pin count, replacement age.
- Dirty entries are never silently discarded after failed writeback.

### VFS Mount, Node, and File

- Mount owns filesystem-private state and operation table.
- Node describes a validated object without exposing disk location.
- File stores node, checked byte offset, mode, and validity generation.
- Unmount flushes before invalidating references.

### Command Context

- Current directory, 256-byte line buffer, token spans, mount, and console sinks.
- Handlers receive bounded parsed arguments and return stable result classes.

## Cross-Entity Invariants

1. Every visible regular file has exactly one valid committed companion immediately before its primary.
2. Directories have no companions in v1.
3. No cluster is owned by two live chains.
4. No FAT/data access uses unvalidated geometry or cluster values.
5. A durability acknowledgement follows successful ordered metadata/data, cache, and device flushes.
6. Ordinary commands never bypass VFS; diagnostics are validated and read-only.
