# Shell-Facing Service Contract: Extension Search

## Request

The request is a versioned fixed-size structure containing user extension text and length. It contains no hash field.

## Dispatch

The caller must have nonzero shell process and application identities. The kernel validates structure bounds and delegates to `vfs_search_extension` on the mounted root. The VFS/filesystem layers own canonicalization, hashing, validation, traversal, and exact matching.

## Reply

The versioned fixed-size reply contains operation status, zero to 16 display-safe absolute locations, an item count, and a truncation flag. The reply type has no extension or hash field. Every unused entry and reserved byte remains zero.

## Security and Correctness Invariants

- Ordinary caller memory never receives the canonical extension, computed hash, stored hash, or raw directory record.
- A result is emitted only after filesystem-owned pair validation and exact authoritative extension comparison.
- Failure produces no partially trusted result array.
