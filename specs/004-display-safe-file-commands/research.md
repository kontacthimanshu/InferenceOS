# Research: Display-Safe File Commands

## Displayed-Path Resolution

- **Decision**: Extend the trusted kernel file-view service with bounded displayed-path resolution. It walks display-safe directory entries, applies the same opaque-identity collision ranking as `dir`, and returns the selected object only on an exact label match.
- **Rationale**: File-view already owns conversion from VFS metadata to ordinary extension-hidden names and enforces the metadata boundary.
- **Alternatives considered**: Trying known extensions, parsing filesystem records in CUI, and selecting an arbitrary same-base match were rejected as ambiguous or boundary violations.

## Visible-Name Uniqueness

- **Decision**: New file/directory creation and rename reject any destination whose canonical
  eight-byte base is already occupied, regardless of extension. Existing collision-bearing media
  remains readable through deterministic labels for repair.
- **Rationale**: Once extensions are hidden, the base is the ordinary namespace. Allowing a second
  authoritative 8.3 name with that base would create an ambiguity the user cannot predict.
- **Alternatives considered**: Silently selecting one entry or automatically deleting/renaming an
  existing entry risks data loss; rejecting old disks would prevent safe repair.

## Exact Mutation Target

- **Decision**: Add VFS identity-mutation wrappers plus InferenceOS-FS callbacks that locate the live primary/companion pair from the validated opaque identity before each mutation.
- **Rationale**: VFS enumeration already returns this identity; VFS brackets mutation lifecycle, while filesystem revalidation detects stale or malformed targets and lets existing transactions perform the change.
- **Alternatives considered**: Reconstructing extension-bearing paths duplicates filesystem naming policy and allows a second lookup to diverge from the selected object.

## Rename Semantics

- **Decision**: Resolve the source identity and destination parent, validate an extension-free 8.3 destination base, and merge it with the source's authoritative extension internally.
- **Rationale**: This preserves file type, companion integrity, content, and extension hiding while supporting moves.
- **Alternatives considered**: Exposed destination extensions, dropping the type, and content-based type inference were rejected.

## Character-Write Compatibility

- **Decision**: Do not use an extension/type allowlist. `write` initializes an existing empty file
  or creates a missing extensionless file; `append` reads every existing byte through the resolved
  identity. Both accept only printable ASCII
  plus tab, carriage return, and line feed and return `unexpected_format` otherwise.
- **Rationale**: The CUI hides extensions, and extensions are labels rather than proof of actual
  content. Empty files have no contrary format evidence; non-empty files can be checked without
  exposing metadata or guessing a type.
- **Alternatives considered**: Extension and icon allowlists were rejected because they can deny
  valid text or accept mislabeled binary content. Prefix-only magic sniffing was rejected because
  unsupported bytes may appear later in a file. Blind replacement was rejected because it could
  destroy an existing image or proprietary file.

## Bounds and Compatibility

- **Decision**: Resolve within the complete existing 64-entry CUI directory bound and fail when more entries exist; switch the five affected commands to displayed source names while `create` retains typed creation.
- **Rationale**: Collision ranks require a complete view, and creation uniquely needs an authoritative type.
- **Alternatives considered**: Dynamic allocation, page-local ranking, and dual canonical/displayed operand modes were rejected.

## Collision-Label Lifetime

- **Decision**: Labels are exact and stable for the current complete listing; a synchronous command resolves one label to one identity and revalidates that identity before mutation. A later namespace change may renumber numeric labels.
- **Rationale**: This matches the existing identity-ranked presentation model and prevents command-time retargeting without introducing session caches.
- **Alternatives considered**: Generation-bound per-console snapshots would make stale strings fail after every namespace change but add state and relisting requirements beyond the reported workflow.
