# Contract: InferenceOS-FS and VFS

## Layer Boundary

Generic clients use VFS paths or opaque handles for mount, open/close, read/write/seek, list,
mkdir/remove/rename, metadata, and flush. Only the filesystem driver translates these operations to
records, FAT chains, and block LBAs. Only the block layer talks to virtio-blk.

## Visibility and Commit

A regular file is visible as healthy only when its 32-byte primary is immediately preceded by one
valid committed 32-byte companion. The primary extension is authoritative; hash matches are
prefilters followed by exact comparison. Mutation uses the ordered transitions in `data-model.md`.
Durable success requires every required generation barrier and device flush to succeed.

## Mount Results

- `IOS_MOUNT_RW`: both superblocks and required structures validate; mutation is safe.
- `IOS_MOUNT_DIAGNOSTIC`: bounds are trustworthy but corruption or disagreement makes writes unsafe.
- `IOS_MOUNT_REJECTED`: bounds, arithmetic, or traversal cannot be proven safe.

Registry failure alone disables the registry and selects authoritative scanning. Version 1 never
repairs automatically. Every scan is range bounded; malformed structures return stable diagnostic
errors without out-of-volume access.
