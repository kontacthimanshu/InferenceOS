# VFS Contract: Extension Search

## Operation

`vfs_search_extension(mount, extension, extension_length, entries, capacity, entry_count, truncated)` begins one read-only mount operation and delegates to the mounted driver.

## Inputs

- Active mounted filesystem
- User extension text and explicit bounded length
- Non-null fixed-capacity result array
- Capacity from 1 through the shell reply maximum

## Driver Responsibilities

- Validate and canonicalize filesystem-specific extension syntax.
- Traverse the root and reachable subdirectories without exceeding VFS depth/path limits.
- Never expose internal companion records as entries.
- Validate regular-file primary/companion pairs.
- Compute the query hash internally and compare it as a prefilter.
- Compare authoritative canonical extension length and bytes exactly.
- Produce absolute paths from extension-hidden display components.
- Set truncation only after observing an additional exact match.

## VFS Responsibilities

- Enforce active-mount lifetime around the read-only operation.
- Reject malformed driver results: count beyond capacity, missing NUL termination, non-absolute paths, invalid lengths, or inconsistent truncation.
- Clear result outputs on validation failure.
