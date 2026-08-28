# Research: Extension Search

## Mediated API Shape

- **Decision**: Expose a kernel-owned extension-search request that returns paths; do not expose a hash-computation API to ordinary shell code.
- **Rationale**: Constitution Principles VII and VIII forbid ordinary shell/application views from receiving extension hashes. The shell needs matches, not the intermediate value.
- **Alternatives considered**: Returning the computed hash and letting the shell query candidates; direct filesystem record scanning from CUI code.

## Search Authority and Registry

- **Decision**: Traverse authoritative directory metadata through a VFS operation and make the optional registry irrelevant to correctness.
- **Rationale**: Principle IX requires correct behavior with the registry disabled or unusable. The current registry is disabled by default.
- **Alternatives considered**: Registry-only lookup; rebuilding or enabling the registry as part of search.

## Collision Handling

- **Decision**: Canonicalize the query, compute the configured companion hash, validate each pair, compare hash text, and then compare authoritative extension length and bytes exactly.
- **Rationale**: FNV-1a-32 is the current on-disk algorithm, but all finite-width hashes can collide. Exact equality makes a collision a performance concern rather than a correctness failure.
- **Alternatives considered**: Hash-only equality; changing the on-disk algorithm, which would require a versioned format migration and is outside scope.

## Traversal and Bounds

- **Decision**: Use deterministic depth-first traversal bounded by `IOS_VFS_MAX_DIRECTORY_LEVELS`, `IOS_VFS_PATH_MAX`, and a 16-entry reply. Continue until an extra match proves truncation, then stop.
- **Rationale**: It needs no heap or persisted cursor and gives a truthful completeness signal.
- **Alternatives considered**: Recursive unbounded traversal; dynamic result arrays; pagination with rescanning continuation.

## Display Safety

- **Decision**: Build absolute paths from VFS display base names and never append the authoritative extension.
- **Rationale**: Search output remains consistent with ordinary extension-hidden enumeration. For a single exact extension query, same-directory base-name duplicates cannot represent two distinct healthy 8.3 filenames.
- **Alternatives considered**: Full canonical filenames; opaque object handles only; diagnostic-style hash output.
