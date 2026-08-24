# Data Model: InferenceOS CUI/GUI Filesystem Demonstrator

## Persistent Entities

### InferenceOS-FS Volume

- **Identity**: magic `INFOSFS1`, format version 1, volume serial.
- **Geometry**: 512-byte sectors, 4096-byte clusters, primary/backup superblocks, one FAT, fixed
  4096-sector registry, then data; root cluster 2.
- **Relationships**: owns FAT entries, registry records, directory chains, and file data chains.
- **Validation**: both superblocks, CRC, bounds, fixed-point FAT capacity, non-overlap, root chain.
- **States**: unformatted → formatted → mounted read-write / diagnostic read-only / rejected.

### Superblock

- **Fields**: exact FR-083 layout including geometry, record sizes, registry bounds, algorithm IDs,
  CRC, and trailer.
- **Rules**: reserved bytes zero; primary and backup structural values match; own CRC field is zero
  while calculating CRC-32/ISO-HDLC.

### FAT Entry and Cluster Chain

- **Fields**: 32-bit entry with lower 28-bit free/next/bad/EOC value; upper bits zero.
- **Relationships**: a chain belongs to one file or directory; cluster 2 begins root.
- **Rules**: entries 0/1 reserved; traversal is bounded and loop checked; no duplicate ownership,
  out-of-range link, or bad/reserved cluster in a healthy chain.

### Primary Directory Record

- **Fields**: exact FR-090 32-byte layout: canonical 8.3 name, attributes, first cluster, size.
- **Authority**: definitive filename and extension; hash never overrides it.
- **Rules**: regular file attribute `0x20`, directory `0x10`; empty file uses cluster/size zero;
  canonical name uniqueness is per directory; maximum size `0xFFFFFFFF`.

### Extension-Hash Companion

- **Fields**: exact FR-113 layout: type `0xF1`, version, algorithm, committed flag, extension length,
  primary-name checksum, eight uppercase hash characters, CRC, zeroed reserves.
- **Relationship**: immediately precedes exactly one regular-file primary in the same cluster.
- **Rules**: checksum binds the following 11-byte name; CRC and FNV-1a-32 text validate; directories
  have none; invalid/uncommitted/missing/duplicate companions prevent healthy exposure.
- **States**: absent → uncommitted → committed → uncommitted mutation fence → committed/deleted;
  interruption in an uncommitted state is detectable and not auto-repaired.

### Regular File Entry Set

- **Composition**: one committed companion followed by one primary plus zero or one FAT chain.
- **Visibility**: VFS exposes one file only when the pair validates.
- **Mutation transitions**:
  - create/save: validate → uncommitted fence → data → FAT → primary → committed companion → flush;
  - rename: uncommit → primary name → recomputed companion → commit;
  - delete: uncommit → delete both records → release chain.

### Extension Registry Record

- **Fields**: exact FR-141 32-byte layout, one canonical extension, hash text, last location,
  generation, CRC.
- **Relationship**: belongs to reserved registry region; derived from committed entry sets.
- **Rules**: at most one active record per extension; refresh rather than duplicate; never controls
  identity or health; disable/rebuild on corruption.
- **States**: disabled / enabled-healthy / stale / full / corrupt / rebuilding; every non-healthy
  state falls back to authoritative directory scanning.

## Runtime Entities

### System Module Manifest and Descriptor

- **Manifest entry**: immutable application identity, role, normalized ESP path, byte length, entry
  ABI version, required/optional flag, and SHA-256 digest.
- **Boot descriptor**: identity, role, immutable physical memory range, length, ABI, and digest.
- **Rules**: unique identity and required role; bounded count/length/path; valid static ELF64 image;
  exact digest; no overlap with kernel, framebuffer, boot data, or unavailable memory.
- **Lifecycle**: packaged on ESP → loader-validated and copied → kernel-revalidated → privately
  mapped → runnable process. Required Shell failure stops boot; GUI module failure preserves CUI.

### VFS Mount and VFS Object

- **Mount fields**: filesystem driver, root object, device, mount state, active-operation count.
- **Object fields**: opaque inode-like identity, kind, rights, current location, reference count.
- **Rules**: one root at `/`; path limit 255 bytes and 16 levels; `..` cannot escape root; generic
  objects contain no raw sector-facing API.

### Block Cache Entry and Flush Generation

- **Entry fields**: device, LBA, 512-byte buffer, clean/dirty/error state, pin count, I/O state,
  dirty generation.
- **Barrier**: target generation, pending writes, first error, device-flush result.
- **Transitions**: absent → clean → dirty → writing → clean; failure returns to dirty/error and
  prevents barrier success.

### Process, Handle, and Capability

- **Process**: statically linked ELF64 image, kernel-assigned application identity, private address
  space, kernel/user stacks, handle table, IPC endpoints, scheduler state, and exit status.
- **Handle**: process-local 64-bit slot/generation value resolving to object kind and rights; zero
  invalid; close increments generation.
- **Type capability**: process-scoped authorization for opaque type/icon/routing operations; it is
  neither an extension nor hash and cannot expand trusted application bindings.
- **Scheduler states**: new → runnable → running → blocked on IPC/input/timer → runnable; running →
  exited. Timer preemption returns running processes to the ready queue; exit performs deterministic
  handle, wait, IPC, and memory cleanup.

### Display-Safe File Entry

- **Permitted**: opaque handle, extension-free display name, object kind, size, generic attributes,
  type/icon capability, allowed operations.
- **Forbidden**: canonical 8.3 name, extension bytes/length, hash/algorithm, directory-record or
  cluster locations, registry contents.
- **Collision rule**: stable extension-free labels such as `REPORT`, `REPORT (2)` ordered by an
  internal directory identity; labels do not rename persistent files.

### Shell IPC Message

- **Header**: size, version, request ID, operation, flags, bounded item/byte counts.
- **Relationship**: ring-3 client → trusted Shell endpoint → syscall/VFS → safe reply.
- **Rules**: caller identity is kernel supplied; invalid version/flags/reserved fields fail;
  Shell restart invalidates outstanding channels.

### Graphics Surface, Window, and Input Event

- **Surface**: dimensions, stride, XRGB8888 pixels, owner, dirty regions.
- **Window**: opaque handle, client surface, bounds, z-order, focus, invalidation state.
- **Input**: fixed-width key/pointer event with flags, timestamp ticks, code/button, and relative
  values; keyboard mapping is separate.
- **Rules**: compositor owns clipping and pointer; GUI objects hold no VFS internals; GUI teardown
  returns input ownership to CUI.

## Integrity Relationships

```text
Volume -> FAT -> directory/data cluster chains
Directory -> [Companion + Primary] -> file cluster chain
Primary authoritative extension -> canonical bytes -> companion hash
Process -> handle table -> VFS/window/IPC objects
Application identity -> trusted type bindings -> process-scoped type capabilities
Shell -> safe DTO -> File Explorer/application (never raw extension/hash)
```
