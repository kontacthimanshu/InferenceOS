# Feature Specification: Display-Safe File Commands

**Feature Branch**: `004-display-safe-file-commands`

**Created**: 2026-08-29

**Status**: Draft

**Input**: User description: "Modify write, append, cat, rename, delete, and fileinfo so users select files by their extension-hidden displayed names. Do not embed a file-type allowlist: write initializes an empty file as text, append validates existing text content, cat displays validated text content, and binary/image content is rejected as an unexpected format."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Use the Names Shown by `dir` (Priority: P1)

As a CUI user, I can pass the extension-hidden name shown by `dir` to `write`, `append`, `rename`, `delete`, and `fileinfo` without knowing or guessing the file's hidden extension.

**Why this priority**: Ordinary views intentionally hide extensions, so displayed names must also be usable names or the core file workflow is inconsistent.

**Independent Test**: Create a file with a hidden type, list its directory, and use only the displayed name for each affected command.

**Acceptance Scenarios**:

1. **Given** an empty `/DOCS/REPORT.TXT` exists and `dir /DOCS` displays `REPORT`, **When** the user runs `write /DOCS/REPORT "updated"`, **Then** the file receives exactly that content without exposing or supplying `TXT`.
2. **Given** the same file, **When** the user runs `append /DOCS/REPORT " text"`, **Then** the supplied characters are appended to that file.
3. **Given** the same file, **When** the user runs `fileinfo /DOCS/REPORT`, **Then** the privileged diagnostic describes that exact file.
4. **Given** the same file, **When** the user runs `delete /DOCS/REPORT`, **Then** that exact file is deleted.
5. **Given** no entry named `FILE1` exists, **When** the user runs `write FILE1 "Hello World"`, **Then** an extensionless text file named `FILE1` is created and initialized without a separate `create` command.

---

### User Story 2 - Rename Without Knowing the Type (Priority: P1)

As a CUI user, I can rename a file by supplying its displayed source name and a new extension-hidden base name while the operating system preserves the file's authoritative hidden type.

**Why this priority**: Requiring the hidden extension on either operand would defeat the extension-hidden namespace and could accidentally change the file type.

**Independent Test**: Rename a displayed text file from `REPORT` to `SUMMARY`, verify the listing changes, and verify privileged metadata still reports the original authoritative type.

**Acceptance Scenarios**:

1. **Given** `REPORT` identifies a TXT file, **When** the user runs `rename REPORT SUMMARY`, **Then** the listing contains `SUMMARY`, the file content is unchanged, and its hidden TXT type is preserved.
2. **Given** a destination directory is writable, **When** a user renames a file to an extension-hidden path in that directory, **Then** the file moves while retaining its authoritative type.
3. **Given** the requested typed destination already exists, **When** rename is attempted, **Then** no file is replaced and an already-exists error is returned.

---

### User Story 3 - Enforce Command/File Compatibility (Priority: P1)

As a user, I receive a deterministic error when a character command cannot safely operate on the selected file content, rather than having an image, binary, or proprietary file corrupted.

**Why this priority**: Hidden metadata must not make destructive operations less safe.

**Independent Test**: Initialize an empty file with `write`, append to valid text, and attempt both commands against non-empty binary content using displayed names; verify binary content never changes.

**Acceptance Scenarios**:

1. **Given** a selected writable regular file is empty, **When** `write` supplies supported text bytes, **Then** the command initializes it regardless of hidden extension.
2. **Given** a selected writable regular file is empty or contains only supported text bytes, **When** `append` supplies supported text bytes, **Then** the characters are appended.
3. **Given** a selected file is non-empty when `write` is requested, or contains any unsupported byte when `append` is requested, **Then** `unexpected_format` is returned and no content or metadata changes.
4. **Given** a selected file or volume is read-only, **When** a mutating command is requested, **Then** the command fails and the target remains unchanged.

---

### User Story 4 - Prevent and Safely Handle Hidden-Name Collisions (Priority: P2)

As a CUI user, new creates and renames cannot introduce duplicate displayed base names, while I can still use exact stable labels to repair an older disk that already contains such collisions.

**Why this priority**: Guessing among same-base files could modify or delete the wrong object.

**Independent Test**: Create `REPORT.TXT`, verify creation of `REPORT.LOG` and an extensionless `REPORT` is rejected, verify rename cannot introduce the same displayed base, and inject a legacy collision view to confirm each existing label still selects only its corresponding object.

**Acceptance Scenarios**:

1. **Given** `REPORT.TXT` exists, **When** creation of `REPORT.LOG`, extensionless `REPORT`, or a directory named `REPORT` is attempted, **Then** `already_exists` is returned and no entry is added.
2. **Given** a destination directory already contains the displayed base `REPORT`, **When** another file or directory is renamed to that base, **Then** `already_exists` is returned and both entries remain unchanged.
3. **Given** legacy media displays `REPORT` and `REPORT (2)`, **When** either exact label is supplied to an affected command, **Then** the same opaque object represented by that label is selected.
4. **Given** no displayed entry exactly matches an operand, **When** an affected command runs, **Then** it returns not-found and changes nothing.
5. **Given** a displayed label names a directory, **When** it is passed where a regular file is required, **Then** the command rejects it and changes nothing.

### Edge Cases

- Absolute and relative displayed paths use the caller's current directory and preserve existing `.` and `..` behavior for directory components.
- Displayed path components use ASCII case-insensitive comparison, matching the filesystem's canonical uppercase 8.3 namespace.
- Source operands containing a hidden extension are not treated as ordinary displayed file names.
- A rename destination is an extension-free 8.3 base name; empty, overlong, malformed, ranked-collision-style, or extension-bearing destinations are rejected.
- Read-only files and mounts reject mutating operations before any content or metadata is changed.
- Missing, malformed, unsupported, mismatched, or ambiguous companion metadata prevents the target from being treated as a healthy selectable file.
- Resolution and mutation use one exact object identity for the duration of a command; if that identity is stale when revalidated, the command fails rather than falling back to another name. Numeric labels are interpreted against the current complete directory view and may be renumbered after a separate create, rename, or delete.
- Resolution is bounded and fails safely if a directory cannot be represented completely by the supported CUI view.
- Empty files contain no format evidence and are treated as text by `write` and `append`; hidden type identity is not an authorization input.
- Supported CUI text is printable ASCII plus tab, carriage return, and line feed. Any other existing or supplied byte is an unexpected format.
- File creation and file/directory rename compare the extension-hidden canonical base against every visible entry in the destination directory and reject a duplicate with `already_exists`.
- Relative file creation binds the new directory record to the caller's resolved current-directory identity; it does not reparse the parent from the filesystem root.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: `write`, `append`, `rename`, `delete`, and `fileinfo` MUST accept extension-hidden file paths matching names presented by ordinary directory listings.
- **FR-002**: The CUI MUST delegate displayed-name resolution and operation authorization to a trusted operating-system service and MUST NOT inspect, reconstruct, request, or receive hidden extensions or hashes.
- **FR-003**: The trusted service MUST resolve each displayed source operand to exactly one healthy regular-file object in the shared mounted namespace before performing or diagnosing an operation.
- **FR-004**: Resolution MUST support absolute paths, relative paths, current-directory semantics, ASCII case-insensitive component matching, and stable legacy collision labels such as `REPORT (2)`.
- **FR-005**: Collision labels MUST select the same opaque object identity represented by the corresponding `dir` entry; resolution MUST never guess when no exact displayed label exists.
- **FR-006**: Each affected command MUST verify that the resolved object permits its requested operation and return a deterministic denial when it does not.
- **FR-007**: `write` MUST initialize an existing empty regular file, or create and initialize a missing extensionless file at the supplied path, using supported input text; if creation reports an occupied base after an initial miss, it MUST retry trusted displayed-name resolution rather than return the creation error; it MUST NOT authorize by extension, icon, or embedded type allowlist.
- **FR-008**: `append` MUST accept an empty regular file or read and validate all existing bytes as supported text before mutation; either command MUST reject unsupported supplied or existing bytes as `unexpected_format` without changing content, allocation state, primary metadata, or companion metadata.
- **FR-008a**: `cat` MUST resolve an extension-hidden display path, require read access, validate all content using the supported CUI text policy before producing output, and return `unexpected_format` without partial output for unsupported content. It MUST NOT create or mutate a file or authorize by hidden extension, icon, or embedded type allowlist.
- **FR-009**: `rename <source> <destination>` MUST treat the destination leaf as an extension-hidden base name and preserve the source file's authoritative extension and type metadata.
- **FR-010**: Rename MUST retain collision-safe authoritative identity checks and MUST reject a destination whose extension-hidden base is already occupied by any file or directory.
- **FR-011**: `delete` MUST remove only the exact resolved regular-file object after delete permission is verified.
- **FR-012**: `fileinfo` MUST resolve the displayed name to the exact object before invoking the existing privileged diagnostic authorization and metadata path.
- **FR-012a**: `hashinfo` MUST resolve a healthy file by its extension-hidden displayed path before invoking the existing privileged diagnostic path, while retaining canonical-path fallback when no displayed match exists so privileged damaged-file diagnosis is not regressed.
- **FR-012b**: `fatinfo` MUST resolve a healthy file by its extension-hidden displayed path before invoking the existing privileged diagnostic path, while retaining canonical-path fallback when no displayed match exists so privileged damaged-file diagnosis is not regressed.
- **FR-013**: Ordinary command requests and errors MUST NOT reveal hidden extensions, hashes, companion records, or raw storage locations; privileged `fileinfo` output retains its explicitly authorized diagnostic contract.
- **FR-014**: All mutations MUST continue to use the shared VFS-backed InferenceOS-FS service and preserve existing save, rename, delete, flush, and companion ordering guarantees.
- **FR-015**: The feature MUST NOT change the on-disk format, extension hash algorithm, registry behavior, or minimum supported volume capacity.
- **FR-016**: Resolution and command execution MUST use bounded buffers and deterministic errors for missing, stale, malformed, incompatible, read-only, or unrepresentable targets.
- **FR-017**: Automated validation MUST cover every affected command, relative and absolute paths, text/binary compatibility, hidden-name collisions, missing files, directories, read-only state, rename type preservation, metadata hiding, and both supported compiler configurations.
- **FR-018**: File and directory creation and rename MUST enforce one unique extension-hidden base name per destination directory, regardless of authoritative extension, while existing collision-bearing media remains readable and exactly selectable.
- **FR-019**: `create` and the missing-file fallback of `write` MUST create relative targets under the caller's resolved current-directory identity, and `dir .` MUST enumerate those new records from that same directory.

### Key Entities

- **Displayed File Path**: An absolute or relative path whose leaf is the extension-hidden name shown by the ordinary directory view.
- **Resolved File Object**: The trusted, exact regular-file identity selected from a displayed path, together with its permitted operations and internally verified type.
- **Collision Label**: A stable extension-hidden presentation name such as `REPORT (2)` that distinguishes same-base objects without revealing their types.
- **Command Compatibility Policy**: The operating-system-owned, extension-independent decision based on emptiness, supported text bytes, operation permissions, and read-only state.
- **Rename Destination**: An extension-hidden base path combined internally with the selected source object's authoritative type.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All five affected commands complete their supported workflows using only names copied from `dir`, with zero hidden extension input.
- **SC-002**: 100% of create and rename attempts that would introduce a duplicate displayed base are rejected, while 100% of labels in an injected legacy collision set select the intended object.
- **SC-003**: Across empty, supported-text, and binary-content cases, `write` and `append` produce zero non-empty/binary-file mutations and 100% correct permitted mutations without consulting hidden type identity.
- **SC-004**: Rename preserves content and authoritative type in 100% of same-directory and cross-directory acceptance cases.
- **SC-005**: Missing, stale, incompatible, directory, and read-only operands produce deterministic failures with zero partial content or metadata changes.
- **SC-006**: Ordinary request and error payloads expose zero hidden extensions, hashes, companion bytes, or raw record locations.
- **SC-007**: Focused hosted tests pass under both GCC and Clang, and the existing file lifecycle and diagnostic regression suites remain green.
- **SC-008**: After changing into a subdirectory, eight consecutive relative creates produce eight entries in `dir .` and zero regular-file entries in the root directory.

## Assumptions

- The affected command scope is `write`, `append`, `rename`, `delete`, and `fileinfo`, plus the filesystem namespace uniqueness checks needed by create and file/directory rename; other diagnostic commands retain their current explicit interfaces.
- The exact labels printed by the current directory view, including deterministic numeric collision suffixes, are the user-visible selection contract.
- The current CUI accepts printable ASCII input. Existing text validation additionally permits tab, carriage return, and line feed so previously stored line-oriented text remains appendable.
- Rename changes only the visible base name and location; changing a hidden file type requires a future type-aware conversion workflow.
- Existing CUI directory-view capacity remains the bounded set from which a displayed name can be selected.

## Constitution Check *(mandatory)*

- **Demonstrable OS scope and shared interfaces**: **PASS** — The change repairs core CUI file lifecycle usability without weakening standalone recovery or GUI behavior.
- **VFS-mediated storage**: **PASS** — FR-002 and FR-014 require trusted mediation over the existing shared VFS-backed storage service.
- **Authoritative extension and collision safety**: **PASS** — FR-003, FR-005, FR-009, and FR-010 keep authoritative type identity internal and make selection exact.
- **Durable save and filesystem integrity**: **PASS** — FR-008 and FR-014 prohibit partial incompatible mutations and retain ordered save, rename, and delete behavior.
- **Extension-hidden views**: **PASS** — FR-001, FR-002, and FR-013 make displayed names operational without disclosing extensions or hashes.
- **Application and shell mediation**: **PASS** — FR-002, FR-006, and FR-007 place resolution and compatibility decisions in trusted operating-system policy rather than CUI parsing.
- **Research-gated registry**: **PASS** — The registry and its correctness role are unchanged.
- **Capacity and bounded arithmetic**: **PASS** — FR-015 and FR-016 retain capacity requirements and bounded resolution.
- **GUI/input layering**: **PASS** — No graphics, input, or GUI dependency direction changes.
- **Freestanding C17 and reproducible builds**: **PASS** — FR-017 retains dual-compiler validation and introduces no assembly or hosted runtime dependency.
- **Release claims**: **PASS** — Success criteria are limited to the five named commands and their tested compatibility policy.
