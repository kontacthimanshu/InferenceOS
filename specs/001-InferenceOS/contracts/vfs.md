# VFS Contract

## Boundary

The shell and filesystem-independent kernel use only VFS for ordinary persistent operations. Public handles never expose LBAs, clusters, slots, FAT entries, or companions.

## Paths and Handles

- One mount at `/`; absolute/relative paths plus `.` and `..`; root parent remains root.
- Component-by-component normalization supports at least 255 bytes and 16 levels and rejects escape above root.
- Mount, node, file, and directory references are opaque and generation-checked after unmount.

## Operations

| Group | Operations | Contract |
|---|---|---|
| Lifecycle | mount, unmount, sync | Mount is clean, diagnostic read-only, or rejected; unmount flushes before invalidation. |
| Files | create, open, close, read, write, seek, remove, rename | Synchronous bytes and explicit transferred count; no sparse starting write; append starts at size. |
| Directories | open/list/close, mkdir, remove, resolve | One object per entry set; deleted slots skipped. |
| Metadata | stat | Generic name, type, attributes, size; no raw disk location. |

Result classes include success, not found, exists, invalid name/path/argument, not mounted, read-only, not empty, no space, range/overflow, corrupt, unsupported, I/O timeout/failure, busy, and invariant failure.

## Adapter Obligations

- Validate private metadata before returning generic objects.
- Never expose a regular file without a valid committed pair.
- Verify authoritative extension/name after hash prefiltering.
- Propagate mutation and flush errors without claiming success.

## Diagnostic Extension

A separate typed read-only InferenceFS interface supplies validated `fsinfo`, `fileinfo`, `hashinfo`, and `fatinfo` data. It cannot mutate or replace ordinary VFS calls.
