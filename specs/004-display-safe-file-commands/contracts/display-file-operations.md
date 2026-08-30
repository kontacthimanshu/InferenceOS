# Contract: Display-Safe CUI File Operations

## User-Facing Commands

```text
write <display-path> "<text>"
append <display-path> "<text>"
cat <display-path>
rename <source-display-path> <destination-display-path>
delete <display-path>
fileinfo <display-path>
hashinfo <display-or-canonical-path>
fatinfo <display-or-canonical-path>
```

`display-path` uses labels printed by `dir`. A ranked label containing spaces is one quoted operand:

```text
write "REPORT (2)" "replacement"
```

The CUI passes operand text and command intent to trusted services. It does not receive an extension, hash, canonical name, or internal type identity during ordinary resolution.

## Resolution Contract

For each component, the trusted resolver MUST normalize path syntax, enumerate the complete bounded VFS directory view, derive the same stable collision labels as `dir`, require an ASCII case-insensitive label and exact kind match, keep type metadata internal, and return opaque object/parent identities with allowed operations.

No match returns `IOS_E_NOT_FOUND`; a directory where a file is required returns `IOS_E_INVALID_ARGUMENT`; an incomplete view returns `IOS_E_NO_SPACE`; stale or malformed objects return the corresponding not-found, protocol, or corruption error.

## Authorization

| Command | Required permission | Additional rule |
|---|---|---|
| `write` | read + write when existing | target empty, or missing path creates an extensionless file; supplied bytes are supported text |
| `append` | read + write | supplied bytes and all existing bytes are supported text |
| `cat` | read | all bytes are supported text; validate before producing output |
| `rename` | rename | any healthy file; preserve type |
| `delete` | delete | any healthy file |
| `fileinfo` | read/select | diagnostic authority remains mandatory |
| `hashinfo` | read/select | diagnostic authority remains mandatory; canonical fallback remains available |
| `fatinfo` | read/select | diagnostic authority remains mandatory; canonical fallback remains available |

Supported text is printable ASCII plus tab, carriage return, and line feed. Hidden extensions,
icons, and internal type identities do not authorize these commands. Empty files are treated as
text, and a missing `write` path creates an extensionless text file. A non-empty `write` target or unsupported supplied/existing byte returns
`IOS_E_UNEXPECTED_FORMAT`; read-only storage retains `IOS_E_READ_ONLY`. Denial occurs before
mutation.

`cat` uses the same displayed-name resolver and supported-text definition. It validates the entire
file before sending content to the console, returns `IOS_E_UNEXPECTED_FORMAT` without partial output
for binary content, and never creates or mutates a file.

## Identity Mutation Contract

VFS brackets the requested identity mutation against the active mount. Before mutation, InferenceOS-FS maps the opaque identity to the live primary record, locates and validates its companion, confirms regular-file kind, and verifies writable state.

- Content validation uses bounded VFS reads against the resolved opaque identity.
- Initial write/append reuse existing content and metadata ordering.
- Delete reuses companion invalidation, primary deletion, barriers, and chain release.
- Rename validates destination parent/base, preserves authoritative extension bytes, and reuses existing commit ordering.
- A stale identity never falls back to a name lookup.

Collision labels are evaluated against the current complete directory view. One synchronous command remains bound to its resolved identity; a later create, rename, or delete may legitimately renumber labels in a new listing.

## Rename Destination

The destination parent is resolved through the displayed namespace. Its leaf is an extension-free 8.3 base and is not interpreted as a collision rank.

```text
rename REPORT SUMMARY
rename /DOCS/REPORT /ARCHIVE/SUMMARY
```

An existing authoritative destination returns `IOS_E_ALREADY_EXISTS`; no overwrite occurs.
An occupied displayed base also returns `IOS_E_ALREADY_EXISTS`, even when the existing entry has a
different extension or is a directory. This rule applies to file and directory creation and rename.
Legacy collision-bearing directories remain enumerable and exact labels remain resolvable.

## Metadata Boundary

Ordinary request, success, and error paths expose no canonical extensions/names, hashes, companions, internal types, record locations, FAT data, clusters, or blocks. After displayed-name resolution, `fileinfo` may return its existing capability-protected diagnostic result.
