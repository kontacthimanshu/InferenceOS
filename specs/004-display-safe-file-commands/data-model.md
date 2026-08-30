# Data Model: Display-Safe File Commands

## Displayed File Path

- **Meaning**: An absolute or relative path accepted from an affected CUI command.
- **Fields**: normalized components and a displayed leaf label.
- **Validation**: at most 255 bytes and 16 levels; the leaf must exactly match a complete display-safe directory entry.
- **Privacy**: contains no extension, hash, companion data, or canonical record bytes.

## Display-Safe Candidate

- **Fields**: extension-free base, opaque object identity, kind, internal type identity, type prefilter, allowed operations, attributes, and byte size.
- **Validation**: nonzero identity, supported kind, bounded base, valid masks, and nonzero type metadata for files.
- **Presentation state**: base label → collision-ranked label (`REPORT`, `REPORT (2)`, ...), ordered by opaque identity.
- **Boundary**: type identity and prefilter stay inside trusted code.

## Resolved File Object

- **Fields**: object identity, parent directory identity, byte size, trusted type identity retained for non-command consumers, and allowed operations.
- **Validation**: exact displayed-label match, expected kind, requested operation allowed, and complete bounded view.
- **States**: unresolved → resolved against the current complete view; resolved → denied on incompatible operation/type; resolved → stale on failed identity revalidation; resolved → mutated only after all checks pass. A later namespace change creates a new view and may renumber labels on legacy collision-bearing media.
- **Visible-name invariant**: new file/directory creation and rename compare the eight-byte canonical base across all visible destination entries and reject an occupied base regardless of extension.

## Command Compatibility Decision

- **Inputs**: command, byte size, supplied bytes, existing bytes when required, operation mask, and read-only state.
- **Supported text bytes**: printable ASCII (`0x20`-`0x7E`), tab, carriage return, and line feed.
- **Write rule**: an existing target must be empty; a missing path creates an extensionless file; supplied bytes must be supported text.
- **Append rule**: supplied bytes must be supported text; a non-empty target is read completely by opaque identity and every existing byte must be supported text.
- **Type independence**: extension, icon, internal type identity, and type prefilter are not compatibility inputs. An empty file is accepted regardless of hidden type because it has no content evidence.
- **Other commands**: rename/delete require matching operation bits; fileinfo requires diagnostic authority after resolution.
- **Output**: permitted or deterministic unexpected-format, access, read-only, not-found, protocol, or bounds error.

## Identity Mutation Request

- **Fields**: object identity, operation, optional offset/read buffer, optional mutation bytes, and optional destination parent/base.
- **Validation**: identity maps to a live regular primary, its companion is healthy, the destination is valid, and the mount is writable.
- **Atomicity**: validation failure changes nothing; success reuses existing ordered content, FAT, primary, companion, barrier, and release transitions.

## Rename Transition

```text
display source resolved
        |
        v
source identity revalidated ----failure----> unchanged
        |
        v
destination parent + base validated --------> unchanged on failure
        |
        v
source extension merged internally
        |
        v
existing ordered rename/move transaction
        |
        v
renamed object with unchanged content and type
```
