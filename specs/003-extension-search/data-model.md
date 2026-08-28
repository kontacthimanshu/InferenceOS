# Data Model: Extension Search

All entities are transient and fixed-capacity. This feature changes no on-disk structure.

## Extension Search Request

- `size`, `version`: wire-structure validation
- `flags`: must be zero
- `extension_length`: length of user text, including an optional leading dot
- `extension`: bounded ASCII input buffer
- Reserved bytes: must be zero

Validation: exactly one non-empty extension; optional single leading dot; one to three canonical characters; ASCII letters, digits, underscore, or hyphen only; no embedded dot or path separator.

## Canonical Extension Query

- `bytes[3]`: uppercase authoritative comparison bytes
- `length`: one through three
- `hash_text[8]`: uppercase hexadecimal FNV-1a-32 representation

Lifecycle: created inside InferenceOS-FS from a validated request and destroyed when the call returns. It never crosses into ordinary shell-visible reply memory.

## Validated File Candidate

- Object identity
- Primary disk record
- Companion disk record
- Decoded primary name and extension
- Display base name

Validation: regular file; committed companion; supported versions/algorithm; valid CRC and association; stored hash equals the recomputed hash for the primary extension. Matching additionally requires query hash equality and exact authoritative extension equality.

## Search Result Location

- `object_identity`: internal result identity for validation and future operations
- `location_length`: length excluding terminator
- `location[256]`: absolute, NUL-terminated, extension-hidden display path

Validation: begins with `/`; contains only display-safe components; contains no extension separator; fits `IOS_VFS_PATH_CAPACITY`.

## Search Reply

- `size`, `version`: wire-structure validation
- `flags`: includes only the defined truncation flag
- `status`: operation result
- `item_count`: zero through 16
- `entries[16]`: search result locations

State transitions:

1. Empty reply initialized.
2. Validated locations appended in deterministic traversal order.
3. If a seventeenth match is found, truncation is set and traversal stops.
4. Reply is rendered only after a successful service return.
