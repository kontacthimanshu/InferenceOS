# Feature Specification: InferenceOS CUI/GUI Filesystem Demonstrator
**Feature Branch**: `001-inferenceos`
**Created**: 2026-08-23
**Status**: Draft
**Input**: User description: "Build InferenceOS as an x86-64 UEFI operating-system demonstrator with both CUI and GUI, a shared VFS backed by InferenceOS-FS, a distinct 32-byte extension-hash companion directory record for every regular file, a research-gated extension registry, extension-hidden File Explorer and application views, shell-mediated system-call access for the demonstrator, at least 50 GB of persistent InferenceOS-FS storage, and project-owned source under src using freestanding C17 with controlled GCC/Clang extensions and minimal x86-64 assembly."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Boot into the CUI and start the GUI (Priority: P1)
As a user or evaluator, I can boot InferenceOS into a usable character command environment and start a graphical desktop from that environment so that both required interfaces operate over the same system state.
**Why this priority**: Every storage, filesystem, application, and GUI demonstration depends on the operating system booting reliably and preserving the CUI as the recovery path.
**Independent Test**: Boot the reference image, reach the `InferenceOS>` prompt, run basic commands, start the GUI, open a GUI terminal, interact with the desktop using keyboard and pointer input, and return to a usable CUI after closing or failing the GUI.
**Acceptance Scenarios**:
1. **Given** a valid reference boot image, **When** the system starts, **Then** it reaches a usable CUI without requiring the GUI.
2. **Given** the CUI is usable, **When** the user starts the GUI, **Then** the graphical desktop becomes usable without replacing the mounted VFS namespace.
3. **Given** the GUI is running, **When** the user opens the GUI terminal, **Then** the terminal uses the same command parser, command registry, and current runtime namespace abstraction as the standalone CUI; persistent filesystem namespace coherence is validated after the shared VFS is introduced in User Stories 2 and 11.
4. **Given** GUI initialization fails, **When** the failure is detected, **Then** the CUI remains usable and reports a diagnostic.
---
### User Story 2 - Format and use a 50 GB-or-larger InferenceOS-FS volume (Priority: P1)
As a user or evaluator, I can format and mount a persistent InferenceOS-FS volume of at least 50 GB so that the filesystem experiment operates at the required storage scale.
**Why this priority**: InferenceOS-FS and the 50 GB minimum are non-negotiable properties of the first usable release.
**Independent Test**: Attach a blank persistent disk of at least 50,000,000,000 bytes, format it as InferenceOS-FS, mount it through the VFS, inspect capacity and free space, unmount it cleanly, and remount it. File lifecycle and reboot durability are validated independently by User Stories 3 and 11.
**Acceptance Scenarios**:
1. **Given** a supported blank device of at least 50,000,000,000 bytes, **When** formatting is explicitly requested, **Then** a valid InferenceOS-FS volume is created.
2. **Given** a valid InferenceOS-FS volume, **When** the volume is mounted, **Then** the VFS exposes it as the persistent root namespace.
3. **Given** a candidate persistent root device smaller than 50,000,000,000 bytes, **When** it is selected, **Then** the system rejects it as below the first-release minimum.
4. **Given** a mounted volume, **When** storage information is requested, **Then** the system reports total capacity, usable capacity, free space, filesystem version, and allocation geometry.
---
### User Story 3 - Save a file with a primary record and extension-hash companion (Priority: P1)
As an application, I can save a regular file and have InferenceOS persist both its normal file metadata and the required extension-hash companion metadata so that the file is represented by a complete InferenceOS-FS entry set.
**Why this priority**: The companion extension-hash directory record is the defining experiment of InferenceOS-FS.
**Independent Test**: Save `REPORT.TXT` through the hosted VFS/filesystem integration harness, inspect the persisted primary and companion records through test-only block-image inspection, verify canonical extension and hash vectors, and inject each commit-phase failure. User-facing diagnostics and reboot durability are validated independently by User Stories 9 and 11.
**Acceptance Scenarios**:
1. **Given** a valid writable file, **When** the application requests a durable save, **Then** file content and primary filesystem metadata are persisted through the normal save path.
2. **Given** content persistence succeeds, **When** the required post-persistence extension-hash operation executes, **Then** the associated extension-hash companion record is persisted before final durable-save success is returned.
3. **Given** `REPORT.TXT`, **When** its extension hash is calculated, **Then** the canonical input is `TXT` and excludes the base name and separator dot.
4. **Given** a complete regular-file entry set, **When** directory enumeration occurs, **Then** the VFS exposes one file rather than two directory objects.
5. **Given** the companion persistence step fails, **When** the save sequence ends, **Then** the file is not reported as a healthy fully committed regular file.
6. **Given** file content changes without an extension change, **When** the file is saved again, **Then** the extension hash remains unchanged.
7. **Given** the extension changes during rename, **When** rename commits, **Then** the extension-derived companion metadata is recomputed.
---
### User Story 4 - Render Extension-Hidden File Entries (Priority: P1)
As an ordinary user, I can view supplied display-safe file entries graphically without seeing file extensions or extension hashes, while File Explorer shows an appropriate type icon and keeps every visible item independently selectable.
**Why this priority**: The presentation model must enforce hidden metadata before it is connected to live Shell/VFS enumeration in User Story 5.
**Independent Test**: Supply File Explorer with display-safe test entries representing several internal types, render the view, verify extension-hidden names, deterministic collision labels, mapped and fallback icons, and confirm that no File Explorer surface reveals extension or hash fields.
**Acceptance Scenarios**:
1. **Given** a display-safe entry corresponding internally to `REPORT.TXT`, **When** File Explorer renders it, **Then** the ordinary display name is `REPORT` and no `.TXT` or hash field is present in the supplied model.
2. **Given** display-safe entries with different opaque type capabilities, **When** File Explorer renders them, **Then** it may use different file-type or application icons without receiving either extension or hash.
3. **Given** a supplied display-safe entry collection, **When** File Explorer renders it, **Then** only ordinary file/directory objects appear and companion records cannot be represented by the ordinary entry schema.
4. **Given** no specific icon mapping exists for a file type, **When** File Explorer renders the file, **Then** it uses a generic icon without revealing the extension.
5. **Given** two internal files would have the same extension-hidden base display name, **When** File Explorer lists them, **Then** both remain independently selectable through a deterministic disambiguation that does not reveal extension or hash.
6. **Given** an ordinary properties view, **When** the user inspects a file, **Then** extension and extension hash remain hidden unless the user explicitly enters a privileged diagnostic surface.
---
### User Story 5 - Search and render File Explorer through the Shell broker (Priority: P1)
As the GUI File Explorer, I can ask the InferenceOS Shell to obtain directory-view, type-view, search, and rendering data so that the demonstrator uses the shell-mediated system-call flow required by the constitution.
**Why this priority**: Shell-mediated File Explorer system calls are a non-negotiable first-demonstrator behavior and are intended to evolve into direct OS APIs later.
**Independent Test**: Start File Explorer, request a directory listing and a file-type view, trace the request through the Shell broker, and verify that the returned File Explorer model contains display-safe names and icons without raw extensions or hashes.
**Acceptance Scenarios**:
1. **Given** File Explorer requests a directory view, **When** the Shell broker processes the request, **Then** it invokes the documented operating-system service and returns display-safe entries.
2. **Given** the registry is disabled or unavailable, **When** File Explorer requests a file-type view, **Then** the Shell obtains the correct visible result from authoritative filesystem metadata.
3. **Given** File Explorer searches for one opaque file type, **When** the OS uses an extension hash as a prefilter, **Then** it verifies the authoritative extension internally before returning an exact match.
4. **Given** the graphical view needs to be rendered, **When** the File Explorer requests rendering through the demonstrator Shell path, **Then** the request uses the documented GUI service boundary instead of raw framebuffer or filesystem access.
---
### User Story 6 - Maintain the optional Extension Registry (Priority: P3)
As a filesystem researcher, I can enable a registry of observed file extensions so that I can measure whether one registry entry per extension improves file-type enumeration or search.
**Why this priority**: The constitution explicitly marks this optimization as optional and research-gated; filesystem correctness must not depend on it.
**Independent Test**: Enable registry research mode, create several files with the same extension and several with different extensions, verify Shell-backed File Explorer type views and authorized diagnostics, reboot with stale registry state to verify authoritative fallback, compare registry-enabled and registry-disabled behavior, and measure the declared performance counters.
**Acceptance Scenarios**:
1. **Given** the registry is enabled and no `TXT` registry entry exists, **When** the first committed `TXT` file is saved, **Then** one registry entry for that canonical extension is created.
2. **Given** the `TXT` registry entry already exists, **When** another committed `TXT` file is saved, **Then** the same logical registry entry is refreshed or overwritten rather than adding a duplicate extension-type entry.
3. **Given** the registry is missing, disabled, stale, corrupt, or full, **When** normal file lookup occurs, **Then** authoritative primary and companion records still provide correct filesystem behavior.
4. **Given** a registry rebuild is requested, **When** committed directory metadata is scanned, **Then** the registry is reconstructed from authoritative file records.
5. **Given** registry research results are evaluated, **When** the enabled and disabled paths are compared, **Then** conditional branches, instructions, CPU cycles where measurable, and end-to-end search latency are reported.
6. **Given** the proposed cycle or branch benefit is not demonstrated, **When** release defaults are selected, **Then** the registry remains disabled by default without blocking the release.
7. **Given** File Explorer requests known file types while the Extension Registry is enabled, **When** the Shell handles the request, **Then** the OS may traverse validated registry records and return opaque type identifiers and icon mappings without returning raw extensions or hashes.
8. **Given** the optional registry is stale after reboot, **When** filesystem use resumes, **Then** registry state cannot override authoritative primary and companion records.
9. **Given** registry research mode is enabled, **When** an authorized registry diagnostic is requested, **Then** registry health and validated entries can be inspected without changing authoritative file state.
---
### User Story 7 - Let proprietary applications receive only their supported files (Priority: P2)
As a proprietary application, I can request the files the operating system knows I am programmed to handle, without receiving the hidden extension or hash, so that the application can present its own embedded file viewer.
**Why this priority**: Application-specific file discovery is a non-negotiable part of the InferenceOS trust model.
**Independent Test**: Configure a test proprietary application with a trusted set of file types, create mixed files, invoke its Shell-mediated file-list request, and verify that only supported files are returned without extension or hash disclosure.
**Acceptance Scenarios**:
1. **Given** a proprietary application has trusted file-type bindings, **When** it asks the Shell for its supported files, **Then** the Shell invokes the appropriate OS service using the application's identity rather than a caller-supplied raw extension.
2. **Given** mixed file types in one directory, **When** the application requests its files, **Then** only files matching its trusted type bindings are returned.
3. **Given** returned file descriptors or view models, **When** the application inspects them, **Then** raw extension and extension hash are absent.
4. **Given** two internal types have a hash collision, **When** one type is requested, **Then** the OS verifies the authoritative extension before returning files.
5. **Given** an application attempts to fabricate an opaque type token, **When** it requests files, **Then** the token does not grant access to unregistered types.
---
### User Story 8 - Let custom applications use approved proprietary APIs (Priority: P2)
As a custom application developer, I can work with file formats supported by proprietary applications through those applications' approved APIs, while the operating system prevents my application from discovering hidden extension or hash values.
**Why this priority**: The constitution requires custom applications to rely on the OS and proprietary application integration rather than extension-based file discovery.
**Independent Test**: Run a test custom application against a test proprietary adapter, verify that raw extension/hash queries fail, verify arbitrary extension filters are unavailable, and verify that an approved proprietary API can operate on an opaque file handle.
**Acceptance Scenarios**:
1. **Given** an ordinary custom application, **When** it requests a raw file extension or extension hash, **Then** the request is unavailable or denied.
2. **Given** a custom application supplies an arbitrary extension or hash as a native file-type selector, **When** the request is processed, **Then** the OS rejects it.
3. **Given** a supported proprietary application exposes an approved API, **When** a custom application uses that API, **Then** the integration may operate on opaque file/content handles without exposing the hidden extension or hash.
4. **Given** no approved proprietary API exists for a requested proprietary format, **When** a custom application attempts the operation, **Then** InferenceOS does not invent or imply an integration contract.
---
### User Story 9 - Inspect hidden filesystem metadata through diagnostics (Priority: P2)
As an OS developer or filesystem evaluator, I can explicitly inspect file extension, extension hash, companion association, allocation chain, and filesystem health so that the filesystem experiment remains observable despite hiding those details from ordinary applications.
**Why this priority**: The constitution separately requires hidden ordinary presentation and observable diagnostic behavior.
**Independent Test**: Create a file, invoke CUI and GUI diagnostic inspectors, correlate the primary record and companion, inspect the cluster chain, then inject malformed metadata and verify the reported error.
**Acceptance Scenarios**:
1. **Given** a healthy file, **When** an authorized hash diagnostic is requested, **Then** it shows the internal extension, canonical hash input, hash algorithm, stored alphanumeric hash, companion version, association validation, and integrity status.
2. **Given** a healthy file, **When** allocation diagnostics are requested, **Then** the ordered cluster chain is shown safely.
3. **Given** a malformed companion, **When** diagnostics inspect the file, **Then** the exact validation failure is reported and the file is not labeled healthy.
4. **Given** ordinary File Explorer or ordinary application APIs, **When** the same file is inspected, **Then** extension and hash remain hidden.
---
### User Story 10 - Create and navigate directories through one VFS namespace (Priority: P2)
As a user, I can create and navigate hierarchical directories from either CUI or GUI so that both interfaces demonstrate one shared namespace backed by InferenceOS-FS.
**Why this priority**: Directory behavior proves that the VFS boundary is more than a root-only file list.
**Independent Test**: Create `/DOCS`, enter it, create a file from CUI, view it from GUI, return to root, then remove the directory after deleting its contents.
**Acceptance Scenarios**:
1. **Given** the persistent root is mounted, **When** a directory is created, **Then** the directory is visible from both CUI and GUI.
2. **Given** a current directory, **When** `.` or `..` is resolved, **Then** path traversal follows the defined hierarchical semantics and cannot escape above `/`.
3. **Given** a non-empty directory, **When** removal is attempted, **Then** it fails with a directory-not-empty result.
4. **Given** directory storage has no suitable free record slots, **When** another entry is created, **Then** directory storage grows safely according to the filesystem allocation rules.
5. **Given** regular-file entries use companion-primary pairs, **When** a directory is enumerated, **Then** the pair is interpreted as one regular file.
---
### User Story 11 - Persist data and recover safely after reboot (Priority: P1)
As a user or evaluator, I can save files, synchronize storage, reboot, and see the same files from both CUI and GUI with valid filesystem metadata.
**Why this priority**: Persistence is necessary to prove that InferenceOS-FS is an on-disk filesystem rather than an in-memory simulation.
**Independent Test**: Create and modify files from CUI and GUI, synchronize, reboot, remount, validate contents and entry sets, start GUI again, and verify the same extension-hidden presentation.
**Acceptance Scenarios**:
1. **Given** file data and metadata have been acknowledged as synchronized, **When** the system reboots and remounts, **Then** the acknowledged contents remain readable.
2. **Given** a regular file was committed, **When** it is reopened after reboot, **Then** its companion record is validated from disk rather than silently regenerated.
3. **Given** an extension-changing rename was committed, **When** the system reboots, **Then** the new internal extension and companion hash remain consistent.
4. **Given** GUI startup after remount, **When** File Explorer opens, **Then** the same files are shown with hidden extensions and correct type icons.
5. **Given** a file is created from the CUI, **When** the live Shell-backed GUI File Explorer refreshes, **Then** it observes the same persistent file.
6. **Given** a file is created or renamed from the GUI, **When** the CUI lists the same directory, **Then** it observes the same persistent object.
---
### User Story 12 - Build the demonstrator reproducibly from source (Priority: P2)
As an external developer, I can build InferenceOS from a clean checkout and launch the reference environment so that the operating-system and filesystem claims are independently reproducible.
**Why this priority**: The project is open source and the constitution makes source layout and C17 toolchain constraints binding.
**Independent Test**: Inspect repository layout, build using the declared primary compiler profile, compile project-owned C with the secondary compiler profile, create the boot and persistent disk artifacts, and run the mandatory demonstration.
**Acceptance Scenarios**:
1. **Given** a clean checkout, **When** project-owned OS source is inspected, **Then** it resides under `src/` except for documented tests, tools, build files, generated artifacts, documentation, and third-party material.
2. **Given** the primary toolchain profile, **When** the project is built, **Then** project-owned C is compiled as freestanding ISO C17.
3. **Given** the secondary compiler profile, **When** project-owned C translation units compile, **Then** no unapproved compiler-specific extension is required.
4. **Given** compiler-specific extensions, **When** source is reviewed, **Then** each extension is covered by the documented allowlist and portability boundary.
5. **Given** x86-64 assembly, **When** source is reviewed, **Then** it is confined to architecture-specific mechanisms rather than filesystem, VFS, GUI, registry, hash, or application policy.
### Edge Cases
- What happens when the persistent disk is absent at boot?
- What happens when the persistent volume is exactly 50,000,000,000 bytes?
- What happens when the reference sparse disk grows but the host runs out of physical storage?
- What happens when the selected logical sector size is not 512 bytes?
- What happens when volume geometry would exceed the supported FAT-derived cluster-number range?
- What happens when primary and backup superblocks disagree?
- What happens when the filesystem signature, version, geometry, or integrity check is invalid?
- What happens when a cluster chain loops, uses a bad or reserved cluster, references cluster 0 or 1 incorrectly, or points outside the data region?
- What happens when file size requires more data than the valid cluster chain supplies?
- What happens when a newly created file has no extension?
- What happens when two different extensions produce the same extension hash?
- What happens when a companion record is missing, duplicated, orphaned, uncommitted, unsupported, or has invalid integrity data?
- What happens when a companion is not immediately followed by the primary record to which it claims association?
- What happens when only one 32-byte directory slot remains but a regular file requires a two-record pair?
- What happens when the end of a directory cluster is reached with insufficient space for a complete companion-primary pair?
- What happens when file allocation succeeds but companion-primary commit fails?
- What happens when file content persistence succeeds but the post-persistence companion update fails?
- What happens when the companion update succeeds but the final device flush fails?
- What happens when an extension-changing rename is interrupted between primary and companion updates?
- What happens when deletion is interrupted after one record has been marked deleted?
- What happens when a write would exceed the maximum file size representable by the primary record?
- What happens when disk-full occurs while extending a fragmented file?
- What happens when File Explorer refreshes while the CUI creates, renames, or deletes a file?
- What happens when CUI and GUI concurrently request different changes to the same file?
- What happens when `REPORT.TXT` and `REPORT.LOG` would both be shown as `REPORT` because extensions are hidden?
- What happens when File Explorer has no icon mapping for a known internal file type?
- What happens when a privileged diagnostic view reveals extension/hash while ordinary application APIs remain open?
- What happens when the Extension Registry is disabled, full, stale, corrupt, or contains a type for which no committed file remains?
- What happens when multiple files with the same extension repeatedly refresh one registry entry?
- What happens when registry-derived results disagree with authoritative directory metadata?
- What happens when a proprietary application requests a type for which it has no trusted binding?
- What happens when a custom application attempts to query raw extensions, raw hashes, or arbitrary native type filters?
- What happens when a custom application requests a proprietary format for which no approved proprietary API is available?
- What happens when GUI initialization fails after the CUI is usable?
- What happens when graphics output works but pointer input does not?
- What happens when the pointer works but no usable font can be loaded?
- What happens when a window is moved partly or completely outside the visible desktop?
- What happens when a command line exceeds the supported input length?
- What happens when a block operation returns a short transfer, timeout, or error?
- What happens when a compiler inserts unexpected padding into an on-disk structure?
- What happens when GCC and Clang disagree on an on-disk structure size or offset?
- What happens when an unapproved compiler extension appears in project-owned source?
- What happens when project-owned OS source is placed outside `src/`?
## Requirements *(mandatory)*
### Functional Requirements
#### System Scope, Source, and Build Constraints
- **FR-001**: InferenceOS MUST target x86-64 as the first usable architecture.
- **FR-002**: InferenceOS MUST boot through UEFI in the reference environment.
- **FR-003**: InferenceOS MUST provide both a standalone CUI and a GUI in the first usable release.
- **FR-004**: The CUI MUST remain usable when GUI initialization fails.
- **FR-005**: Project-owned operating-system implementation source MUST reside beneath `src/`.
- **FR-006**: Documentation, tests, host tools, build metadata, generated output, and documented third-party material MAY reside outside `src/`.
- **FR-007**: Project-owned C MUST conform to ISO C17 in a freestanding environment except for explicitly approved compiler extensions.
- **FR-008**: GCC and Clang MUST be supported compiler families for project-owned C.
- **FR-009**: Compiler extensions MUST be recorded in a source-controlled allowlist.
- **FR-010**: Compiler-specific annotations SHOULD be isolated behind compiler or architecture abstraction headers/macros.
- **FR-011**: GNU nested functions, statement expressions, computed goto, zero-length arrays, and variable-length arrays MUST NOT be required by generic kernel/filesystem/GUI code.
- **FR-012**: The freestanding runtime MUST provide the memory/string primitives required by project-owned code and supported compiler-generated calls, including `memcpy`, `memmove`, `memset`, and `memcmp`.
- **FR-013**: x86-64 assembly MUST be limited to architecture-specific mechanisms that cannot reasonably be expressed through the approved C boundary.
- **FR-014**: VFS policy, InferenceOS-FS allocation policy, hashing, CRC logic, extension-registry policy, filename handling, File Explorer policy, GUI layout, rendering algorithms, and application type-routing policy MUST NOT be implemented in assembly.
- **FR-015**: Fixed on-disk structures MUST be protected by build-time size and offset assertions in both supported compiler profiles.
#### Boot, Console, and Dual-Interface Runtime
- **FR-016**: The system MUST reach the CUI without requiring GUI initialization.
- **FR-017**: The CUI MUST support printable ASCII input, Backspace, Enter, command parsing, deterministic error reporting, and return to the prompt after ordinary errors.
- **FR-018**: The CUI command line MUST support at least 255 bytes excluding the terminating null byte.
- **FR-019**: The first usable CUI MUST provide commands equivalent in purpose to `help`, `version`, `clear`, `devices`, `diskinfo`, `format`, `mount`, `unmount`, `fsinfo`, `sync`, `dir`, `cd`, `pwd`, `mkdir`, `rmdir`, `create`, `write`, `append`, `type`, `rename`, `delete`, `fileinfo`, `hashinfo`, `fatinfo`, `gui`, `reboot`, and `shutdown`.
- **FR-020**: Serial or equivalent early diagnostic output MUST remain available for boot, panic, storage, filesystem, and GUI initialization failures.
- **FR-021**: The GUI MUST be startable from a valid system/CUI state.
- **FR-022**: The GUI MUST include a desktop/root graphical workspace, a terminal window, and a File Explorer.
- **FR-023**: The GUI terminal MUST expose the same command semantics and mounted namespace as the standalone CUI.
- **FR-024**: The GUI MUST accept normalized keyboard and pointer movement/button events.
- **FR-025**: GUI failure MUST NOT make the persistent filesystem inaccessible from the CUI recovery path.
- **FR-026**: `reboot` and `shutdown` MUST synchronize required filesystem and block-device state before restart or halt.
#### Shell-Mediated Demonstrator Services
- **FR-027**: The demonstrator MUST provide a Shell program/service that brokers File Explorer and application requests required by the constitution.
- **FR-028**: File Explorer MUST request ordinary file-list, type-list, search, and rendering operations through the Shell broker.
- **FR-029**: The Shell MUST invoke documented operating-system system calls or equivalent kernel service entries to fulfill those requests.
- **FR-030**: Proprietary application programs in the demonstrator MUST begin file-list requests through the Shell broker rather than by supplying raw filesystem extensions or hashes directly to the kernel.
- **FR-031**: Custom application programs in the demonstrator MUST begin supported proprietary-file operations through the Shell broker and the approved proprietary-application integration path.
- **FR-032**: The Shell-mediated demonstrator contract MUST be replaceable by a direct versioned InferenceOS application API in a later commercial/production evolution without changing the InferenceOS-FS on-disk format.
- **FR-033**: The first demonstrator MUST NOT require the future direct application API to be finalized before the Shell-mediated flow is usable.
#### System Call and Kernel Service Boundary
- **FR-034**: InferenceOS MUST provide a documented, versioned system-call or kernel-service mechanism for the Shell and application interactions required by this specification.
- **FR-035**: Kernel entry MUST validate caller-provided pointers, lengths, handles, and operation arguments appropriate to the selected execution model.
- **FR-036**: Application-facing filesystem services MUST use VFS paths or opaque file handles rather than raw filesystem-sector addresses.
- **FR-037**: As the general metadata-boundary rule, every ordinary application-facing service MUST omit raw file extensions and extension hashes; consumer-specific requirements below define additional permitted and forbidden fields.
- **FR-038**: The OS MUST provide services sufficient for durable file save, directory enumeration, file-view search, application-specific file enumeration, GUI view/render requests, and input/event delivery.
- **FR-039**: The constitutionally required post-persistence extension-hash operation MUST execute immediately after successful file-content/primary-metadata persistence and before final durable-save success is returned.
- **FR-040**: When that post-persistence operation begins while execution is already in privileged kernel context, the implementation MAY avoid a redundant privilege transition, but the same ordering, validation, failure behavior, and observability MUST be preserved.
#### VFS and Namespace
- **FR-041**: The kernel MUST provide a mandatory VFS abstraction.
- **FR-042**: CUI, GUI, Shell, File Explorer, and application-facing file services MUST use one shared VFS namespace.
- **FR-043**: The first usable release MUST expose InferenceOS-FS as the persistent root namespace at `/`.
- **FR-044**: The VFS MUST provide mount, unmount, create, open, close, read, write, seek, list-directory, create-directory, remove, rename, metadata-query, and flush operations.
- **FR-045**: Generic VFS clients MUST use opaque file/directory references rather than raw filesystem-sector addresses.
- **FR-046**: Generic callers MUST NOT directly manipulate FAT entries, cluster chains, raw directory records, extension-hash records, filesystem superblocks, registry storage, or physical disk sectors.
- **FR-047**: Filesystem-specific diagnostic operations MUST use an explicitly diagnostic interface and MUST NOT become the ordinary application ABI.
- **FR-048**: `/` MUST be the VFS path separator.
- **FR-049**: Absolute and relative paths MUST be supported.
- **FR-050**: `.` MUST identify the current directory and `..` MUST identify the parent; resolving `..` at `/` MUST remain at `/`.
- **FR-051**: The VFS MUST support at least 255-byte paths and at least 16 directory levels including root.
- **FR-052**: The VFS MUST prevent traversal above the mounted root.
- **FR-053**: CUI and GUI MUST observe committed namespace mutations coherently after required refresh/synchronization.
#### Persistent Storage Capacity and Block Interface
- **FR-054**: The first usable release MUST support an InferenceOS-FS persistent volume with addressable capacity of at least 50,000,000,000 bytes.
- **FR-055**: The reference persistent virtual disk MUST be at least 50 GB; a 64 GiB sparse disk is an acceptable validation profile.
- **FR-056**: The 50 GB minimum MUST apply to the InferenceOS-FS volume itself rather than merely to host free space.
- **FR-057**: Version 1 MUST use 512-byte logical sectors.
- **FR-058**: The formatter MUST reject a device whose logical sector size is not 512 bytes.
- **FR-059**: Version 1 MUST use 4096-byte clusters consisting of eight logical sectors.
- **FR-060**: FAT sizing, sector counts, cluster counts, file offsets, directory offsets, free-space accounting, and device byte offsets MUST use overflow-safe arithmetic.
- **FR-061**: The selected volume geometry MUST keep the supported 50 GB-or-larger reference volume within the version-1 usable FAT-derived cluster-number range.
- **FR-062**: The generic block interface MUST expose block read, block write, flush, logical sector size, total sector count/capacity, and device status.
- **FR-063**: InferenceOS-FS MUST access storage through the generic block interface rather than a controller-specific implementation.
- **FR-064**: The reference environment MAY use a sparse disk representation as long as guest-visible capacity and persistence semantics remain correct.
#### InferenceOS-FS Volume Layout
- **FR-065**: InferenceOS-FS MUST be a distinct filesystem and MUST NOT be advertised as standards-conforming FAT32.
- **FR-066**: InferenceOS-FS version 1 MUST use little-endian encoding for multi-byte on-disk integers.
- **FR-067**: Version 1 MUST reserve logical sector 0 for a primary superblock and logical sector 1 for a backup superblock.
- **FR-068**: Version 1 MUST use one FAT immediately after the two superblock sectors.
- **FR-069**: Version 1 MUST reserve an Extension Registry region immediately after the FAT and before the data region.
- **FR-070**: The Extension Registry region MUST occupy exactly 4096 logical sectors (2 MiB) in version 1 whether or not registry research mode is enabled.
- **FR-071**: The data region MUST begin immediately after the reserved Extension Registry region.
- **FR-072**: The root directory MUST begin at data cluster 2.
- **FR-073**: Each FAT entry MUST occupy 32 little-endian bits; the lower 28 bits carry the allocation value and the upper four bits MUST be zero in version 1.
- **FR-074**: FAT value `0x00000000` MUST identify a free cluster.
- **FR-075**: FAT value `0x0FFFFFF7` MUST identify a bad cluster.
- **FR-076**: FAT values `0x0FFFFFF8` through `0x0FFFFFFF` MUST identify end-of-chain.
- **FR-077**: Valid data-cluster values in the supported range MUST identify the next cluster in a chain.
- **FR-078**: FAT entries 0 and 1 MUST be reserved.
- **FR-079**: On a newly formatted volume, the FAT entry for root cluster 2 MUST be initialized as end-of-chain.
- **FR-080**: Formatter geometry calculations MUST reject overlap, out-of-volume ranges, unsupported cluster counts, and arithmetic overflow.
- **FR-081**: Every FAT or data-region access MUST validate the referenced cluster number before computing disk offsets.
#### InferenceOS-FS Version-1 Superblock
- **FR-082**: The primary and backup superblocks MUST each occupy exactly one 512-byte sector.
- **FR-083**: The version-1 superblock MUST use this fixed layout:
| Offset | Size | Field | Version-1 Meaning |
|---|---:|---|---|
| `0x000` | 8 | Magic | ASCII `INFOSFS1` |
| `0x008` | 2 | FormatVersion | `1` |
| `0x00A` | 2 | HeaderSize | `80` |
| `0x00C` | 2 | BytesPerSector | `512` |
| `0x00E` | 1 | SectorsPerCluster | `8` |
| `0x00F` | 1 | FatCount | `1` |
| `0x010` | 2 | ReservedSuperblockSectors | `2` |
| `0x012` | 2 | PrimaryDirectoryRecordSize | `32` |
| `0x014` | 2 | CompanionRecordSize | `32` |
| `0x016` | 2 | Flags | Version-1 flags |
| `0x018` | 8 | TotalSectors | Total logical sectors in volume |
| `0x020` | 4 | SectorsPerFat | FAT length in sectors |
| `0x024` | 4 | RootCluster | `2` |
| `0x028` | 8 | RegistryStartSector | First registry sector |
| `0x030` | 4 | RegistrySectorCount | `4096` |
| `0x034` | 4 | VolumeSerial | Non-security volume identifier |
| `0x038` | 11 | VolumeLabel | Uppercase ASCII, space padded |
| `0x043` | 1 | HashAlgorithmId | `1` for FNV-1a-32 |
| `0x044` | 2 | CompanionRecordVersion | `1` |
| `0x046` | 2 | RegistryRecordVersion | `1` |
| `0x048` | 4 | SuperblockCRC32 | CRC-32/ISO-HDLC with this field zeroed |
| `0x04C` | 4 | ReservedHeader | Zero in version 1 |
| `0x050` | 430 | Reserved | Zero when formatted |
| `0x1FE` | 2 | TrailerSignature | `0xAA55` little-endian |
- **FR-084**: Mount validation MUST verify the magic, version, header size, sector size, cluster size, FAT count, record sizes, total sectors, FAT bounds, registry bounds, root cluster, hash algorithm, CRC, and trailer signature before trusting derived offsets.
- **FR-085**: The backup superblock MUST contain the same version-1 structural values as the primary superblock.
- **FR-086**: If the primary superblock is invalid while the backup is valid, the system MAY provide diagnostic read-only access but MUST NOT silently mount writable.
- **FR-087**: If both superblocks are valid but disagree on a required structural field, writable mount MUST be refused.
- **FR-088**: Reserved version-1 fields whose interpretation is undefined MUST be zero when accepting nonzero data could change structure interpretation.
#### FAT32-Derived Primary Directory Record
- **FR-089**: Every primary file or directory record MUST occupy exactly 32 bytes.
- **FR-090**: The version-1 primary record MUST use this FAT32-derived layout:
| Offset | Size | Field | Version-1 Use |
|---|---:|---|---|
| `0x00` | 11 | Name | 8-byte base name + 3-byte extension, uppercase ASCII and space padded |
| `0x0B` | 1 | Attributes | Directory or regular-file attributes |
| `0x0C` | 1 | Reserved | Zero |
| `0x0D` | 1 | CreateTenth | Zero in first demonstrator |
| `0x0E` | 2 | CreateTime | Zero in first demonstrator |
| `0x10` | 2 | CreateDate | Zero in first demonstrator |
| `0x12` | 2 | AccessDate | Zero in first demonstrator |
| `0x14` | 2 | FirstClusterHigh | Bits 16..31 of first cluster |
| `0x16` | 2 | WriteTime | Zero in first demonstrator |
| `0x18` | 2 | WriteDate | Zero in first demonstrator |
| `0x1A` | 2 | FirstClusterLow | Bits 0..15 of first cluster |
| `0x1C` | 4 | FileSize | Regular-file byte length; zero for directories |
- **FR-091**: Version 1 MUST use attribute `0x10` for directories and `0x20` for regular files.
- **FR-092**: A primary record whose first byte is `0x00` MUST mark the logical end of used directory records.
- **FR-093**: A primary or companion slot whose first byte is `0xE5` MUST be treated as deleted/reusable.
- **FR-094**: Regular-file primary records MUST always be immediately preceded by their required extension-hash companion record.
- **FR-095**: Directory primary records MUST NOT require extension-hash companions in version 1.
- **FR-096**: An empty regular file MUST use first-cluster value 0 and size 0 until data storage is allocated.
- **FR-097**: Version 1 MUST enforce a maximum regular-file size of `0xFFFFFFFF` bytes and fail before an operation would exceed it.
- **FR-098**: A new regular file MUST have two consecutive available 32-byte directory slots for companion plus primary.
- **FR-099**: A companion-primary pair MUST NOT cross the logical end of a directory cluster; if only one slot remains, the pair MUST begin in an extended directory cluster.
#### Filename and Extension Rules
- **FR-100**: Version 1 MUST support FAT32-style short 8.3 internal names.
- **FR-101**: A regular-file base name MUST contain 1 through 8 supported characters and MAY contain an extension of 0 through 3 supported characters.
- **FR-102**: User-entered lowercase ASCII letters MUST be converted to uppercase before internal storage and comparison.
- **FR-103**: Supported version-1 filename characters MUST include uppercase `A-Z`, digits `0-9`, underscore `_`, and hyphen `-`.
- **FR-104**: The separator dot MUST be a presentation/parser separator and MUST NOT be stored in the 11-byte primary name field.
- **FR-105**: Unused base and extension bytes MUST be padded with ASCII space `0x20`.
- **FR-106**: Internal filename comparison MUST apply the same uppercase canonicalization as creation.
- **FR-107**: Additional dots, path separators inside a component, unsupported/control characters, overlength base names, and overlength extensions MUST fail without silent truncation.
- **FR-108**: Duplicate canonical 8.3 names within one directory MUST be rejected.
- **FR-109**: Ordinary UI/application presentation MUST hide the internally stored extension even though the primary record remains authoritative.
- **FR-110**: First-demonstrator proprietary-application type bindings MUST use file types representable by the version-1 internal extension model; broader proprietary formats requiring longer extensions are a later filesystem/application-contract evolution.
#### Extension-Hash Companion Record
- **FR-111**: Every committed regular file MUST have exactly one companion record; its physical placement is governed by FR-094.
- **FR-112**: The companion record MUST occupy exactly 32 bytes.
- **FR-113**: The version-1 companion record MUST use this layout:
| Offset | Size | Field | Version-1 Meaning |
|---|---:|---|---|
| `0x00` | 1 | RecordType | `0xF1` |
| `0x01` | 1 | RecordVersion | `1` |
| `0x02` | 1 | HashAlgorithmId | `1` for FNV-1a-32 |
| `0x03` | 1 | Flags | Bit 0 = committed; bits 1..7 zero |
| `0x04` | 1 | ExtensionLength | Canonical extension length `0..3` |
| `0x05` | 1 | PrimaryNameChecksum | Checksum of following primary 11-byte name |
| `0x06` | 2 | Reserved0 | Zero |
| `0x08` | 8 | ExtensionHashText | Eight uppercase hexadecimal ASCII characters |
| `0x10` | 4 | RecordCRC32 | CRC-32/ISO-HDLC with this field zeroed |
| `0x14` | 12 | Reserved1 | Zero in version 1 |
- **FR-114**: A version-1 companion MUST be recognized only when record type, version, hash algorithm, flags, reserved fields, hash text, and CRC are valid.
- **FR-115**: The companion committed flag MUST be set before the following primary file can be exposed as a healthy committed regular file.
- **FR-116**: `PrimaryNameChecksum` MUST be calculated over the following primary record's exact 11 stored name bytes using the FAT/VFAT-style rotate-right-and-add 8-bit checksum algorithm.
- **FR-117**: A companion whose primary-name checksum does not match the following primary record MUST be treated as corrupt.
- **FR-118**: A companion not immediately followed by a valid regular-file primary record MUST be treated as orphaned or corrupt.
- **FR-119**: A regular-file primary record not immediately preceded by a valid committed companion MUST be treated as incomplete or corrupt.
- **FR-120**: Deleting a regular file MUST mark or release both companion and primary records.
- **FR-121**: The VFS MUST expose a valid companion-primary pair as one regular file.
#### Extension Canonicalization, Hashing, and Integrity
- **FR-122**: The canonical extension MUST be derived from the three extension bytes of the authoritative primary name.
- **FR-123**: Canonicalization MUST remove only trailing ASCII-space padding from the three stored extension bytes.
- **FR-124**: Canonical extension bytes MUST be uppercase ASCII.
- **FR-125**: The base filename and separator dot MUST NOT participate in the extension hash.
- **FR-126**: An empty extension MUST hash the zero-length byte sequence and record extension length 0.
- **FR-127**: Hash algorithm identifier 1 MUST mean FNV-1a-32 with offset basis `0x811C9DC5` and prime `0x01000193`.
- **FR-128**: FNV-1a-32 MUST process each canonical extension byte by XOR then 32-bit modular multiplication by the FNV prime.
- **FR-129**: The resulting 32-bit value MUST be rendered as exactly eight uppercase hexadecimal ASCII characters `0-9A-F` in the companion record.
- **FR-130**: Hash collisions MUST NOT affect filename uniqueness, file-type equality, lookup correctness, rename correctness, or deletion correctness.
- **FR-131**: Any hash-based prefilter or classification MUST verify the authoritative primary extension before returning an exact type match.
- **FR-132**: Changing file content or file size without an extension change MUST NOT require a hash change.
- **FR-133**: Changing only the base name MUST preserve the extension hash but MUST update companion association/integrity data as required.
- **FR-134**: Changing the extension MUST recompute canonical extension, extension length, extension hash text, association checksum, and companion CRC.
- **FR-135**: Superblock, companion-record, and registry-record integrity MUST use CRC-32/ISO-HDLC with reflected polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, and final XOR `0xFFFFFFFF`.
- **FR-136**: A structure's own CRC field MUST be treated as zero during CRC calculation.
- **FR-137**: A CRC mismatch MUST cause the affected structure to be reported invalid rather than silently accepted.
#### Extension Registry Region and Records (Research-Gated)
- **FR-138**: The Extension Registry region MUST be reserved in every version-1 volume but registry use MUST be disabled by default until the research gate passes.
- **FR-139**: The registry region MUST be interpreted as an array of 32-byte registry records.
- **FR-140**: One active registry record MUST represent at most one canonical extension.
- **FR-141**: The version-1 registry record MUST use this layout:
| Offset | Size | Field | Version-1 Meaning |
|---|---:|---|---|
| `0x00` | 1 | RecordType | `0xE1` |
| `0x01` | 1 | RecordVersion | `1` |
| `0x02` | 1 | Flags | Bit 0 = active |
| `0x03` | 1 | ExtensionLength | Canonical length `0..3` |
| `0x04` | 1 | HashAlgorithmId | `1` for FNV-1a-32 |
| `0x05` | 3 | Reserved0 | Zero |
| `0x08` | 3 | CanonicalExtension | Uppercase ASCII, space padded |
| `0x0B` | 1 | Reserved1 | Zero |
| `0x0C` | 8 | ExtensionHashText | Eight uppercase hexadecimal ASCII characters |
| `0x14` | 4 | LastDirectoryCluster | Cluster containing most recently associated primary record |
| `0x18` | 2 | LastDirectorySlot | Slot index of most recently associated primary record |
| `0x1A` | 2 | UpdateGeneration | Wrapping non-security update generation |
| `0x1C` | 4 | RecordCRC32 | CRC-32/ISO-HDLC |
- **FR-142**: Creating the first committed file of a canonical extension while registry research mode is enabled MUST create one active registry record for that extension.
- **FR-143**: Creating another committed file of an already registered extension MUST refresh or overwrite the same logical registry entry rather than create a duplicate type entry.
- **FR-144**: Registry state MUST be treated as derived and rebuildable rather than authoritative filesystem state.
- **FR-145**: Registry absence, disablement, staleness, corruption, or exhaustion MUST NOT prevent correct file lookup through primary/companion directory metadata.
- **FR-146**: The filesystem MUST support rebuilding the registry by scanning committed regular-file entry sets.
- **FR-147**: Registry records MUST remain internal to the OS/filesystem and MUST NOT expose extension or hash values through ordinary application APIs.
- **FR-148**: Registry research MUST compare enabled and disabled behavior using the same dataset and reference environment.
- **FR-149**: Research results MUST report conditional branches, total instructions, CPU cycles where measurable, and end-to-end File Explorer/type-search latency.
- **FR-150**: The claim that registry reuse saves a conditional jump or CPU cycle MUST remain a hypothesis until measurement demonstrates the claimed benefit.
- **FR-151**: Registry use MAY become enabled by default only when repeatable benefit is demonstrated without correctness regression or unacceptable update/storage overhead.
#### Directory and File Operations
- **FR-152**: The root directory MUST be stored as a normal cluster chain beginning at cluster 2.
- **FR-153**: Subdirectories MUST be stored as cluster chains and MUST contain internal `.` and `..` directory references.
- **FR-154**: Directory enumeration MUST safely skip deleted slots and stop at a valid end marker unless the directory chain terminates first.
- **FR-155**: Directory scanning MUST recognize primary records, extension-hash companions, and internal directory entries and MUST reject unsupported record types rather than treating them as filenames.
- **FR-156**: File creation MUST create a valid uncommitted companion, create the following primary record, then mark the companion committed only after the pair is internally consistent and its CRC has been recomputed.
- **FR-157**: The physical write order MUST make interrupted creation detectable as incomplete on the next scan.
- **FR-158**: File deletion MUST make the file inaccessible as committed metadata before releasing clusters and MUST never free clusters owned by another file.
- **FR-159**: Rename MUST fail when the destination canonical internal name already exists.
- **FR-160**: Rename across extensions MUST update primary and companion metadata as one logical operation.
- **FR-161**: A failed rename MUST NOT report success unless the resulting primary/companion pair validates together.
- **FR-162**: File read/write operations MUST validate offsets, file size, cluster numbers, arithmetic bounds, and block ranges.
- **FR-163**: File writes MUST allocate and link additional clusters when required.
- **FR-164**: Deleting or truncating file data MUST free each owned cluster at most once.
- **FR-165**: Newly allocated data clusters MUST be zero-initialized before unwritten bytes can be returned through the VFS.
- **FR-166**: Sparse files are not supported in version 1.
- **FR-167**: A write whose starting offset is greater than current file size MUST be rejected rather than creating a sparse region.
- **FR-168**: Append MUST begin exactly at current file size.
#### Save, Synchronization, and Recovery
- **FR-169**: Application save MUST enter the Shell-mediated or documented file persistence service path.
- **FR-170**: The save path MUST persist requested file content and authoritative primary metadata according to the filesystem write-ordering rules.
- **FR-171**: The application-save service MUST apply the ordering defined by FR-039 and MUST NOT introduce another persistent operation between final primary-metadata persistence and companion persistence.
- **FR-172**: Final durable-save success MUST NOT be returned until the required companion record and required device flush have succeeded.
- **FR-173**: If companion persistence fails, the save MUST return a defined failure and leave either the prior valid state or a detectably incomplete new state.
- **FR-174**: Crash/interruption recovery MUST distinguish healthy committed pairs from incomplete save states.
- **FR-175**: The kernel MUST track dirty filesystem data/metadata through a block cache or equivalent buffering layer.
- **FR-176**: `sync` MUST persist required file data, primary records, companion records, enabled registry metadata, FAT state, and block-device cache state in the documented order.
- **FR-177**: `unmount` MUST complete or safely refuse outstanding operations, flush required state, and invalidate the mount.
- **FR-178**: A failed write or flush MUST prevent false durable-success reporting.
- **FR-179**: Successfully acknowledged and synchronized file data and metadata MUST survive an orderly reboot.
#### Mount Validation and Corruption Handling
- **FR-180**: Mount MUST validate superblock geometry before reading FAT, registry, directory, or data regions.
- **FR-181**: Mount MUST validate the FAT chain required to access the root directory before exposing the filesystem.
- **FR-182**: Writable mount MUST fail when structural corruption could make mutation unsafe.
- **FR-183**: Diagnostic read-only access MAY be provided for corrupt volumes when volume bounds can still be established safely.
- **FR-184**: FAT traversal MUST use a bounded loop-detection method and MUST never traverse more clusters than the volume contains.
- **FR-185**: FAT traversal MUST reject out-of-range, reserved, bad, malformed, or impossible next-cluster values.
- **FR-186**: A regular file whose size exceeds the capacity of its valid cluster chain MUST be reported corrupt.
- **FR-187**: A file MAY have unused bytes in its final allocated cluster without being considered corrupt.
- **FR-188**: The system MUST distinguish healthy writable mount, diagnostic/read-only mount, and rejected mount states.
- **FR-189**: Automatic repair is not required for the first usable release.
#### Extension-Hidden CUI and GUI Presentation
- **FR-190**: Ordinary GUI File Explorer MUST NOT display file extensions.
- **FR-191**: Ordinary GUI File Explorer MUST NOT display extension hashes.
- **FR-192**: Ordinary CUI directory listing MUST hide file extensions by default.
- **FR-193**: Ordinary displayed file names MUST be derived from the internal base-name representation rather than the full extension-bearing internal name.
- **FR-194**: If hiding extensions would create duplicate visible names, the UI MUST apply deterministic extension-free disambiguation.
- **FR-195**: File Explorer MUST choose an application-specific or file-type-specific icon using OS-internal authoritative type information.
- **FR-196**: If no specific icon exists, File Explorer MUST use a generic file icon without revealing extension or hash.
- **FR-197**: Companion records MUST never appear as separate ordinary files.
- **FR-198**: Applying FR-037 to user interaction surfaces, ordinary properties, clipboard metadata, drag/drop metadata, File Explorer view models, and ordinary application file-list results MUST NOT leak raw extension or hash values.
- **FR-199**: Explicit kernel-owned diagnostic tools MAY reveal internal extension/hash values.
- **FR-200**: Diagnostic permission to reveal extension/hash MUST NOT implicitly grant ordinary applications the same metadata.
#### File Explorer Search, Type Enumeration, and Rendering
- **FR-201**: File Explorer MUST receive directory-view data from the Shell broker rather than reading raw filesystem structures.
- **FR-202**: A display-safe File Explorer entry MUST contain an opaque file identity/handle, extension-hidden display name, object kind, permitted size/metadata, and opaque icon/type identifier.
- **FR-203**: File Explorer type-enumeration and search requests MUST NOT require File Explorer to send or receive raw extension or extension-hash values.
- **FR-204**: With the research registry enabled, the OS MAY traverse registry records to enumerate known file types for File Explorer.
- **FR-205**: Registry-backed type enumeration MUST return opaque type identifiers and icon mappings rather than raw extensions or hashes.
- **FR-206**: With the registry disabled or unusable, authoritative directory scanning MUST provide equivalent correct visible File Explorer results.
- **FR-207**: Type search MAY use extension hashes internally as prefilters but MUST verify authoritative extension bytes before returning matches.
- **FR-208**: File Explorer rendering requests MUST use the Shell-mediated GUI service boundary and MUST NOT directly manipulate raw filesystem structures.
- **FR-209**: The future conversion from Shell-mediated calls to direct OS-specific APIs MUST preserve the same extension-hidden and type-routing semantics.
#### Proprietary Application File Discovery
- **FR-210**: InferenceOS MUST provide a trusted mapping between each supported proprietary application identity and the internal file types it is programmed to handle.
- **FR-211**: A proprietary application MUST ask the Shell for supported files by application identity/capability rather than by supplying raw extension or hash filters.
- **FR-212**: The OS MUST return only files matching the application's trusted type bindings.
- **FR-213**: Applying FR-037 to proprietary-application enumeration, file-list results MUST omit raw extensions and hashes.
- **FR-214**: Results MAY include opaque file identity/handle, extension-hidden display name, opaque file-type/icon token, content-access handle, and permitted generic metadata.
- **FR-215**: Hash collisions MUST be resolved against the authoritative extension before a file is returned.
- **FR-216**: Fabricating or guessing an opaque type token MUST NOT grant access to additional file types.
#### Custom Application and Official-API Boundary
- **FR-217**: Applying FR-037 to custom applications regardless of implementation language, no OS/Shell operation available to them may reveal raw file extensions or extension hashes.
- **FR-218**: Ordinary custom applications MUST NOT be able to request native file enumeration by supplying arbitrary hidden extensions or hashes.
- **FR-219**: For a supported proprietary file type, custom applications MUST use the approved official API/integration contract of the proprietary application to perform operations that require proprietary-format understanding.
- **FR-220**: InferenceOS MUST NOT invent, reverse engineer, or represent an unofficial proprietary interface as an official API.
- **FR-221**: Approved proprietary integrations MAY operate on opaque file/content handles without exposing hidden extension/hash data to the custom application.
- **FR-222**: The application contract MUST make OS/proprietary routing authoritative rather than caller-supplied extension discovery.
- **FR-223**: Hidden extension/hash behavior MUST be documented as an application/UI contract rather than a cryptographic secrecy guarantee against privileged diagnostics or raw-disk examination.
#### GUI Architecture and Behavior
- **FR-224**: The GUI MUST use a layered architecture consisting of a graphics-device/framebuffer abstraction, 2D rendering, window management/composition, widgets/controls, and desktop/application surfaces.
- **FR-225**: The graphics layer MUST support pixel-addressable output, filled rectangles, line/border drawing, raster text, and pointer rendering.
- **FR-226**: The input layer MUST normalize keyboard and pointer events for GUI consumers.
- **FR-227**: The window system MUST support at least one movable/managed application window, stacking order, clipping, repaint/invalidation, and a root desktop surface.
- **FR-228**: The GUI MUST include a terminal window.
- **FR-229**: The GUI MUST include a File Explorer.
- **FR-230**: GUI File Explorer MUST use Shell/VFS services and MUST NOT depend on the raw InferenceOS-FS layout.
- **FR-231**: InferenceOS-FS MUST NOT depend on GUI state or contain window, icon, coordinate, color, or rendering policy.
- **FR-232**: The exact reference graphics adapter, pointer device, resolution, pixel format, font, event ABI, and compositor strategy MUST be fixed by the implementation plan without changing these behavioral requirements.
#### Diagnostics
- **FR-233**: `fsinfo`-equivalent diagnostics MUST report filesystem identity, format version, volume capacity, cluster/FAT geometry, free-space state, primary/companion record sizes, hash algorithm, and registry state.
- **FR-234**: `fileinfo`-equivalent diagnostics MUST report internal canonical name, object type, attributes, size, first cluster, primary-record location, and companion-record location for regular files.
- **FR-235**: `hashinfo`-equivalent diagnostics MUST report internal extension, canonical extension bytes, extension length, hash algorithm, stored alphanumeric hash, recomputed hash, record version, committed state, association checksum status, CRC status, and overall validation result.
- **FR-236**: `fatinfo`-equivalent diagnostics MUST display a bounded validated file/directory cluster chain and its end-of-chain state.
- **FR-237**: Registry diagnostics MUST report whether the registry is enabled, its health, active type count, and record validation status.
- **FR-238**: Diagnostic output MUST NOT read beyond validated filesystem bounds or disclose unrelated kernel-memory contents.
#### Validation and Reproducibility
- **FR-239**: Tests MUST cover boot to CUI, GUI startup, GUI failure recovery, keyboard input, pointer input, basic rendering, window management, GUI terminal, and File Explorer.
- **FR-240**: Tests MUST cover formatting, mounting, and free-space accounting on a volume of at least 50,000,000,000 bytes.
- **FR-241**: Tests MUST cover regular-file create, write, append, read, extension-preserving rename, extension-changing rename, delete, directory create/navigation/remove, synchronization, reboot persistence, and unmount.
- **FR-242**: Tests MUST verify that every committed regular file has one valid primary record and one valid companion record.
- **FR-243**: Tests MUST cover empty, one-character, two-character, and three-character extensions.
- **FR-244**: Tests MUST cover same-extension files, different-extension files, and deterministic injected hash collisions.
- **FR-245**: Tests MUST cover missing, orphaned, duplicate, unsupported, uncommitted, bad-CRC, association-mismatched, and extension-hash-mismatched companions.
- **FR-246**: Tests MUST cover invalid superblocks, inconsistent primary/backup superblocks, invalid FAT entries, looped chains, out-of-range clusters, impossible geometry, and capacity arithmetic overflow.
- **FR-247**: Tests MUST verify ordinary CUI/GUI listings, File Explorer results, proprietary-application results, and custom-application services do not expose raw extension/hash values.
- **FR-248**: Tests MUST verify diagnostic surfaces can expose extension/hash only through explicit diagnostic paths.
- **FR-249**: Tests MUST cover hidden-name collisions such as `REPORT.TXT` and `REPORT.LOG`.
- **FR-250**: Tests MUST cover File Explorer icon/type mapping and generic-icon fallback.
- **FR-251**: Tests MUST cover proprietary application type filtering and custom application restrictions.
- **FR-252**: Registry research tests MUST cover create, same-extension refresh/overwrite, rebuild, disabled state, full state, stale/corrupt state, and authoritative fallback.
- **FR-253**: Registry benchmarking MUST compare enabled and disabled paths and report branch, instruction, cycle where measurable, and end-to-end search-latency measurements.
- **FR-254**: All project-owned C translation units MUST compile under both declared GCC and Clang freestanding C17 validation profiles.
- **FR-255**: The build MUST fail when an on-disk structure's size or required field offset differs from this specification.
- **FR-256**: Repository validation MUST detect project-owned OS implementation source placed outside `src/`.
- **FR-257**: A clean checkout MUST provide one documented workflow that produces boot artifacts, a reference persistent disk of at least 50 GB, symbols/debug artifacts as applicable, and launch instructions.
- **FR-258**: Release documentation MUST distinguish mandatory filesystem behavior from optional research-gated registry behavior.
- **FR-259**: Release documentation MUST NOT claim a conditional-jump or CPU-cycle benefit before the research measurements demonstrate it.
- **FR-260**: Release documentation MUST clearly state that the first demonstrator uses extension-hidden application/UI contracts and that privileged diagnostics or raw-disk analysis can still reveal internal metadata.
### Key Entities
- **InferenceOS-FS Volume**: The versioned persistent filesystem containing superblocks, one FAT, the reserved Extension Registry region, cluster-addressed directory/data storage, and at least 50 GB of supported capacity.
- **Superblock**: The 512-byte volume descriptor that identifies the filesystem and defines version, geometry, record sizes, root cluster, registry location, and hash algorithm.
- **FAT Entry**: A 32-bit allocation record whose lower 28 bits encode free, next-cluster, bad-cluster, reserved, or end-of-chain state.
- **Cluster**: A 4096-byte allocation unit containing regular-file data or directory records.
- **Primary Directory Record**: The authoritative 32-byte FAT32-derived file/directory record containing the internal 8.3 name, attributes, first cluster, and file size.
- **Extension-Hash Companion Record**: The distinct 32-byte record immediately preceding a regular-file primary record and containing the alphanumeric extension hash plus version, association, and integrity metadata.
- **Regular File Entry Set**: One valid committed companion record followed by its authoritative primary regular-file record.
- **Canonical Extension**: The internal normalized extension bytes derived from the authoritative primary record.
- **Extension Hash Text**: The eight-character uppercase hexadecimal representation of the version-1 FNV-1a-32 hash.
- **Extension Registry Region**: The fixed, research-gated metadata region reserved after the FAT and before the data region.
- **Extension Registry Record**: A rebuildable 32-byte record representing one canonical extension and refreshed when another file of the same extension commits.
- **VFS Mount**: The shared namespace attachment exposing InferenceOS-FS at `/`.
- **VFS File Reference**: An opaque file/directory reference used by generic OS clients rather than raw filesystem addresses.
- **Shell Broker**: The first-demonstrator service/program that receives File Explorer and application requests and invokes the corresponding kernel services.
- **Display-Safe File Entry**: An extension-hidden view object containing opaque identity, display name, type/icon token, and permitted metadata.
- **Opaque File-Type Identifier**: A non-extension, non-hash token used by File Explorer and proprietary application contracts.
- **Application Type Binding**: The trusted association between a proprietary application identity and the internal file types that InferenceOS may return to it.
- **Custom Application**: An application that cannot discover raw extension/hash values and relies on OS routing plus approved proprietary APIs where proprietary-format knowledge is required.
- **Diagnostic Inspector**: An explicit CUI/GUI diagnostic surface allowed to reveal internal extension/hash and filesystem record information.
- **Graphics Surface**: A pixel-addressable GUI rendering target managed through the graphics/windowing subsystem.
- **Input Event**: A normalized keyboard or pointer event consumed by CUI or GUI components.
## Success Criteria *(mandatory)*
### Measurable Outcomes
- **SC-001**: The reference build reaches a usable standalone CUI in 20 consecutive clean boot attempts without manual debugger intervention.
- **SC-002**: The GUI can be started successfully after CUI boot in 20 consecutive clean runs, and an intentionally failed GUI initialization leaves the CUI usable in 100% of injected failure tests.
- **SC-003**: A persistent volume of at least 50,000,000,000 bytes can be formatted, mounted, used, synchronized, rebooted, and remounted successfully.
- **SC-004**: After 20 orderly save/synchronize/reboot cycles, every acknowledged test file retains byte-for-byte expected content and remains visible from both CUI and GUI.
- **SC-005**: 100% of committed regular files in the lifecycle test are represented as one visible file and validate as a complete primary-plus-companion entry set.
- **SC-006**: 100% of supported test extensions produce the independently expected eight-character alphanumeric hash representation.
- **SC-007**: 100% of injected hash-collision tests preserve correct file identity and type routing.
- **SC-008**: Ordinary File Explorer views, ordinary CUI directory listings, proprietary-application file lists, and custom-application file services expose zero raw file extensions and zero extension-hash values in the defined test corpus.
- **SC-009**: Explicit diagnostic views can correlate a regular file with its internal extension, extension hash, primary record, companion record, first cluster, and cluster chain without requiring a host-side disk editor.
- **SC-010**: A file created from CUI appears in GUI after refresh, and a file renamed from GUI appears in CUI with the same persistent content, in 100% of integration scenarios.
- **SC-011**: When hidden extensions would produce duplicate visible base names, every file remains independently selectable in 100% of collision-display tests.
- **SC-012**: File Explorer displays the configured application/file-type icon for every registered test type and falls back to a generic icon for every unregistered type without revealing extensions.
- **SC-013**: A proprietary test application receives only its trusted file types in 100% of type-filtering cases.
- **SC-014**: A custom test application cannot obtain raw extensions, raw extension hashes, or arbitrary hidden-type enumeration through the ordinary Shell/OS contract.
- **SC-015**: With the Extension Registry disabled, all mandatory file, VFS, CUI, GUI, File Explorer, and application-routing tests still pass.
- **SC-016**: With registry research mode enabled, repeated creation of files sharing one extension leaves one active logical registry entry for that extension.
- **SC-017**: Registry benchmarking produces a reproducible enabled-versus-disabled report containing conditional-branch count, instruction count, CPU-cycle measurement where available, and end-to-end type-search latency.
- **SC-018**: Every injected missing, orphaned, duplicate, unsupported, bad-integrity, association-mismatched, and extension-hash-mismatched companion case is detected and never reported as a healthy committed file.
- **SC-019**: Every injected invalid-superblock, impossible-geometry, FAT-loop, out-of-range-cluster, and bad/reserved-cluster misuse case is detected without an out-of-volume access.
- **SC-020**: An external evaluator following repository documentation can build the project from a clean checkout, create/attach the required persistent disk, boot to CUI, start the GUI, create and save a file, inspect the hash diagnostically, view the extension-hidden File Explorer representation, reboot, and verify persistence.
## Assumptions
- The constitution v2.1.0 is the governing authority and takes precedence over older specifications where the architectural direction changed.
- Older InferenceOS specifications are technical continuity sources only where compatible with the current constitution.
- The former 64-byte primary-record design is superseded by one 32-byte extension-hash companion and one distinct 32-byte primary record.
- The first usable demonstrator targets x86-64 UEFI in a documented virtual-machine reference environment.
- A separate UEFI-readable boot filesystem is available; firmware is not expected to understand InferenceOS-FS.
- The reference persistent disk may be a sparse 64 GiB virtual disk while guest-visible capacity still satisfies the 50 GB minimum.
- Version 1 uses 512-byte sectors, 4096-byte clusters, one FAT, primary and backup superblocks, and a fixed 2 MiB Extension Registry region.
- Version 1 uses internal FAT32-style 8.3 names to preserve the authoritative 32-byte primary-record contract.
- Proprietary application examples describe the routing model and do not imply that Microsoft Word, Microsoft Excel, or their formats are already ported.
- First-demonstrator proprietary support uses explicitly supported proprietary or test adapters and approved APIs.
- The extension hash uses FNV-1a-32 rendered as eight uppercase hexadecimal ASCII characters.
- CRC-32/ISO-HDLC provides integrity detection and is not a security primitive.
- The authoritative primary extension remains definitive when a hash collision occurs.
- The Extension Registry is reserved on disk but disabled by default until research demonstrates benefit.
- Registry branch/cycle savings remain a research hypothesis rather than an established performance fact.
- Extension/hash hiding applies to ordinary contracts; privileged diagnostics remain an intentional observability exception.
- The Shell is the first-demonstrator broker; a future version may replace it with a direct versioned OS API.
- Exact device models, display properties, syscall numbers, ABI structures, and compositor choices are implementation-plan decisions constrained by this specification.
- Multi-user security, networking, POSIX compatibility, dynamic linking, package management, journaling, snapshots, encryption, compression, and automatic repair are outside the first demonstrator unless separately specified.

## Constitution Check

- **CUI and GUI**: PASS — both are mandatory, and the CUI remains independently usable for recovery.
- **Shared VFS**: PASS — CUI, GUI, Shell, File Explorer, and application file services share one VFS namespace.
- **InferenceOS-FS boundary**: PASS — persistent storage remains behind the VFS and generic block layer and is independent of GUI state.
- **Primary and companion records**: PASS — the records remain distinct, fixed at 32 bytes each, and every committed regular file requires a valid pair.
- **Extension authority and collisions**: PASS — the primary extension is authoritative and every hash-derived match requires exact internal verification.
- **Metadata hiding and mediation**: PASS — ordinary users and applications receive neither raw extensions nor hashes; Shell mediation cannot bypass the VFS.
- **Durability**: PASS — final save success requires content, primary metadata, companion metadata, and required flushes to succeed in the specified order.
- **Extension Registry**: PASS — the registry is optional, derived, non-authoritative, disabled by default, and benchmark-gated.
- **Capacity**: PASS — the persistent InferenceOS-FS volume supports at least 50,000,000,000 bytes.
- **GUI and input layering**: PASS — GUI, graphics, input, terminal, and File Explorer boundaries remain layered.
- **Freestanding implementation**: PASS — project code remains under `src`, uses freestanding C17, supports GCC and Clang, and limits assembly to architecture mechanisms.
- **Release claims**: PASS — the specification distinguishes mandatory behavior from research claims and deferred production capabilities.
