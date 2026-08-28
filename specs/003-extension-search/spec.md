# Feature Specification: Extension Search

**Feature Branch**: `003-extension-search`

**Created**: 2026-08-28

**Status**: Draft

**Input**: User description: "Implement a search command that accepts a file extension, resolves it through the system-call layer, searches the filesystem, and renders matching file locations without revealing the extension hash."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Find Files by Extension (Priority: P1)

As a CUI user, I can enter `search <extension>` and receive a list of file locations whose authoritative extensions exactly match my query.

**Why this priority**: Returning accurate matches is the primary user value and provides a usable search command on its own.

**Independent Test**: Create matching and non-matching files in the root and nested directories, run `search .doc`, and verify that only DOC file locations appear.

**Acceptance Scenarios**:

1. **Given** files with DOC and TXT extensions exist in multiple directories, **When** the user enters `search doc`, **Then** the CUI renders every reachable DOC file location and no TXT file location.
2. **Given** the same files, **When** the user enters `search .DoC`, **Then** the result is identical because extension input is case-insensitive and may start with a dot.
3. **Given** no reachable file has the requested extension, **When** the user searches for it, **Then** the command succeeds and renders an explicit no-matches message.

---

### User Story 2 - Preserve Hidden Metadata (Priority: P2)

As a CUI user, I see display-safe file locations but never the canonical extension or its computed or stored hash in ordinary search output.

**Why this priority**: Metadata hiding and OS-controlled type selection are binding InferenceOS interface guarantees.

**Independent Test**: Search for an extension with known matching files and verify that command output contains their extension-hidden locations but contains neither the extension suffix nor hexadecimal hash text.

**Acceptance Scenarios**:

1. **Given** `/WORK/REPORT.DOC` is a valid file, **When** the user runs `search doc`, **Then** the result includes `/WORK/REPORT` and does not expose `.DOC` or a hash.
2. **Given** the shell requests a search, **When** the system-call layer completes it, **Then** the shell receives only bounded display-safe result records and search status, not raw filesystem records, extensions, or hashes.

---

### User Story 3 - Remain Correct Under Collisions and Damage (Priority: P3)

As a user, search never returns a file merely because its companion hash equals the query hash; the filesystem must also verify the authoritative extension and companion health.

**Why this priority**: Hashes are derived metadata and correctness must remain independent of collision probability or registry state.

**Independent Test**: Present a candidate whose hash matches the query but whose authoritative extension differs, and verify that it is not returned; repeat with the optional registry disabled.

**Acceptance Scenarios**:

1. **Given** a hash-prefilter candidate with a different authoritative extension, **When** the search runs, **Then** that file is excluded.
2. **Given** a malformed, unsupported, missing, or mismatched companion, **When** the search encounters it, **Then** the entry is not reported as a healthy match and the search does not expose its raw metadata.
3. **Given** the Extension Registry Block is disabled, missing, stale, corrupt, or full, **When** a search runs, **Then** correct results are still produced from authoritative file metadata.

### Edge Cases

- Empty extensions, a dot with no following characters, unsupported characters, embedded dots, path separators, and extensions longer than the filesystem limit are rejected with command usage guidance.
- Leading-dot and non-leading-dot forms are equivalent; ASCII letters are canonicalized case-insensitively.
- Search includes regular files in the mounted root and all reachable subdirectories, but never returns directories or companion records.
- Directory traversal and result storage are bounded. When more matches exist than one reply can contain, the command reports that results were truncated rather than silently claiming completeness.
- A directory cycle, invalid cluster chain, excessive nesting, or path that cannot fit in a display-safe result is handled as a filesystem/search error without overrunning buffers.
- Search is read-only and does not create, rename, update, repair, or persist metadata.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The CUI MUST provide `search <extension>` and document it in command help.
- **FR-002**: The command MUST accept one extension in `DOC` or `.DOC` form, canonicalize ASCII letter case, and reject invalid or additional arguments.
- **FR-003**: The shell MUST submit the extension search through the shell-facing system-call layer rather than inspect filesystem records directly.
- **FR-004**: The system-call layer MUST validate and canonicalize the extension, compute its configured extension hash internally, and use the hash only as a candidate prefilter.
- **FR-005**: For every candidate, InferenceOS-FS MUST validate the primary/companion association and compare the authoritative canonical extension exactly before returning a match.
- **FR-006**: Search MUST traverse the mounted VFS namespace recursively and return regular-file matches from the root and reachable subdirectories.
- **FR-007**: The VFS and filesystem implementation MUST remain the only persistent-storage path used by the search service.
- **FR-008**: The system-call reply MUST be a bounded array of display-safe file locations plus status, count, and explicit truncation state.
- **FR-009**: Ordinary search output and the shell-visible reply MUST NOT contain raw extensions, computed hashes, stored hashes, companions, or raw directory records.
- **FR-010**: The CUI MUST render one returned location per line and render an explicit no-matches message when the array is empty.
- **FR-011**: Search correctness MUST NOT depend on the optional Extension Registry Block and MUST remain correct when it is unavailable or unusable.
- **FR-012**: Hash collisions MUST NOT cause false matches, hidden matches, replacement, merging, or misclassification.
- **FR-013**: Search MUST be read-only and MUST return a defined error for an unavailable mount, corrupted traversal, invalid result path, or invalid request.
- **FR-014**: The implementation MUST use bounded arithmetic, bounded recursion/traversal, and fixed-capacity freestanding C17 data structures.
- **FR-015**: Automated validation MUST cover command parsing, syscall metadata boundaries, nested traversal, no matches, invalid input, capacity truncation, collision-safe exact comparison, registry-disabled operation, and GCC and Clang builds.

### Key Entities

- **Extension Search Request**: A shell-originated request containing only user-supplied extension text and its length; it never contains or requests a hash.
- **Canonical Extension**: The validated, case-normalized authoritative comparison value used only inside trusted kernel/VFS/filesystem boundaries.
- **Search Result Entry**: A display-safe absolute file location that excludes the file extension and all companion metadata.
- **Search Reply**: A bounded collection of result entries with count, status, and truncation state.
- **Validated File Candidate**: A regular-file primary/companion pair whose structure, association, algorithm, stored hash, recomputed hash, and authoritative extension satisfy filesystem validation.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For a test namespace containing matching and non-matching files across at least three directory levels, search returns 100% of in-capacity exact matches and zero false matches.
- **SC-002**: Searches using `doc`, `.doc`, `DOC`, and `.DoC` return identical locations.
- **SC-003**: Ordinary search reply buffers and rendered output contain zero extension hashes, companion bytes, raw directory records, or returned extension suffixes.
- **SC-004**: A deliberately constructed hash-prefilter false candidate produces zero false-positive results.
- **SC-005**: The same correctness suite passes with the Extension Registry Block disabled.
- **SC-006**: Invalid requests and over-capacity result sets are reported deterministically without buffer overruns, crashes, hangs, or filesystem writes.
- **SC-007**: Hosted tests and boot artifacts build successfully with both supported GCC and Clang configurations.

## Assumptions

- The existing InferenceOS-FS 8.3-derived filename rules remain in force, including a one-to-three-character extension limit and the current supported character set.
- Search starts at the mounted namespace root; a path-scoped search is outside this feature.
- Results are returned in deterministic filesystem traversal order; sorting is outside this feature.
- A fixed reply capacity is acceptable for the demonstrator when truncation is explicit.
- The existing FNV-1a-32 companion algorithm and on-disk format are unchanged; this feature adds no on-disk structures.

## Constitution Check *(mandatory)*

- **Demonstrable OS scope and shared interfaces**: **PASS** — The feature adds a real CUI capability over existing kernel services without weakening CUI recovery or GUI coexistence.
- **VFS-mediated storage**: **PASS** — FR-003, FR-006, and FR-007 prohibit direct shell access to directory records and preserve the VFS/filesystem boundary.
- **Authoritative extension and collision safety**: **PASS** — FR-004, FR-005, and FR-012 make the hash a prefilter and require exact authoritative comparison.
- **Extension-hidden views**: **PASS** — FR-008 through FR-010 restrict ordinary replies to display-safe extension-hidden locations and never expose hashes.
- **Application and shell mediation**: **PASS** — The shell-facing syscall returns OS-selected, display-safe results and does not create an alternate filesystem path.
- **Research-gated registry**: **PASS** — FR-011 requires correct registry-disabled and degraded-registry behavior; no default-enablement claim is made.
- **Durability and format integrity**: **PASS** — Search is read-only, rejects unhealthy candidates, and changes neither save ordering nor the on-disk format.
- **Capacity and bounded arithmetic**: **PASS** — FR-014 requires bounded traversal and result storage while retaining the existing minimum-capacity filesystem contract.
- **GUI/input layering**: **PASS** — No graphics or input dependency direction changes.
- **Freestanding C17 and reproducible builds**: **PASS** — FR-014 and FR-015 retain freestanding C17 and dual-toolchain validation with no new assembly.
- **Release claims**: **PASS** — Success criteria are limited to the implemented search command and explicitly tested behavior.
