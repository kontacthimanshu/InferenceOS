<!--
Sync Impact Report
- Version change: 2.0.0 -> 2.1.0
- Amendment type: MINOR
- Rationale: Reorganizes the complete vision into declarative, testable governance and makes
  application metadata boundaries, shell mediation, save ordering, and the research-gated
  extension registry explicit.
- Modified principles: all nine existing principles were clarified and reorganized.
- Added principles: Application and Shell Mediation; Research-Gated Extension Registry.
- Removed sections: None.
- Follow-up TODOs: None.
-->

# InferenceOS Constitution

## Core Principles

### I. Demonstrable Operating-System Scope

InferenceOS MUST be a bootable, open-source x86-64 operating-system demonstrator using UEFI and
QEMU as its primary reference environment. Its first usable release MUST provide a real kernel, a
standalone character user interface (CUI), a graphical user interface (GUI), persistent storage, a
shared Virtual Filesystem (VFS), and the versioned InferenceOS-FS filesystem.

The release MUST demonstrate file and directory creation, reading, writing, listing, renaming,
deletion, metadata inspection, flushing, and persistence across reboot. It MUST support an
InferenceOS-FS volume of at least 50,000,000,000 bytes.

InferenceOS MUST NOT be represented as production-ready, hardened, multi-user, network-complete,
or a general-purpose replacement for established operating systems unless those capabilities are
separately specified, implemented, and validated.

**Rationale**: The project is a focused operating-system and filesystem experiment. Release claims
must remain limited to demonstrated and tested behavior.

### II. Shared CUI and GUI Environments

The first usable release MUST provide both a standalone CUI and a graphical desktop. The CUI MUST
remain independently usable for recovery, diagnostics, and administration when the GUI is
unavailable or fails to initialize.

The CUI and GUI MUST be presentation layers over the same kernel services and mounted namespace.
Files changed through either interface MUST be visible through the other. The GUI MUST include a
terminal exposing the same command semantics as the standalone CUI and a graphical file browser
supporting navigation, folder creation, rename, deletion, and file inspection.

**Rationale**: Both interfaces must expose one coherent operating system rather than separate
storage implementations or namespaces.

### III. VFS-Mediated Storage Boundary

InferenceOS MUST contain a filesystem-independent VFS. Ordinary CUI commands, GUI components,
applications, and filesystem-independent kernel code MUST access persistent files through it.
InferenceOS-FS MUST implement the VFS contract and access storage through a generic block layer.

```text
CUI / GUI / applications
          |
          v
         VFS
          |
          v
   InferenceOS-FS
          |
          v
 Generic block layer
          |
          v
   Storage driver
```

The VFS MUST provide mount, unmount, create, open, close, read, write, seek, list, make-directory,
remove, rename, metadata-query, and flush operations. Ordinary callers MUST NOT manipulate raw
sectors, allocation tables, cluster chains, directory records, superblocks, or hash companions.
Filesystem diagnostics MAY inspect them only through a validated privileged diagnostic interface.

**Rationale**: The VFS preserves one namespace and prevents presentation code from depending on
filesystem internals.

### IV. Versioned InferenceOS-FS Format

InferenceOS-FS MUST be a distinct, documented, versioned filesystem derived from selected FAT32
concepts. It MAY use FAT-style allocation tables, 32-bit entries, cluster chains, cluster-based
directories, allocation markers, redundant FAT copies, and an FSInfo-like hint. It MUST NOT be
advertised as standards-conforming FAT32.

The format MUST define its unique signature, version, byte order, sector and cluster geometry,
allocation layout, directory rules, capacity calculations, validation, and compatibility behavior.
All byte, sector, cluster, table, directory, file-offset, and capacity arithmetic MUST be
overflow-safe. A firmware-readable EFI System Partition MUST remain separate from the
InferenceOS-FS persistent volume.

**Rationale**: FAT-derived allocation keeps the demonstrator understandable while an explicit
format identity prevents false interoperability claims.

### V. Authoritative Extension and Companion Hash (NON-NEGOTIABLE)

Every committed regular file MUST have two distinct 32-byte directory records: one primary
FAT32-derived record and one associated InferenceOS-FS extension-hash companion record.

The primary record MUST remain authoritative for filename, extension, attributes, first cluster,
file size, and primary metadata. The companion MUST contain a record-type identifier, version,
hash-algorithm identifier, deterministic hash of the canonical extension, unambiguous association,
integrity information, and reserved space for compatible evolution.

The format MUST define byte offsets, widths, byte order, encoding, canonicalization, case rules,
padding, hash algorithm and width, alphanumeric representation, association validation, integrity
validation, and version behavior. Build-time assertions MUST validate fixed sizes and offsets.

The hash is derived metadata. It MUST NOT be treated as a filename, proof of content type, security
credential, unique identifier, or replacement for authoritative extension comparison. Collisions
MUST be assumed possible; exact extension identity MUST be verified internally whenever correctness
depends on file type.

**Rationale**: The companion is the defining filesystem experiment, while authoritative extension
verification preserves correctness.

### VI. Durable Save and Filesystem Integrity (NON-NEGOTIABLE)

A regular-file save MUST persist acknowledged content and allocation state, persist the primary
record, then persist or update its companion before reporting the file as fully committed. A
companion failure MUST prevent exposure as a healthy committed file. An extension-changing rename
MUST recompute extension-derived metadata before commit is reported.

Flushed content, allocation state, primary metadata, and companion metadata MUST survive reboot.
The filesystem MUST detect missing, orphaned, duplicate, ambiguous, malformed, or unsupported
companions; integrity failures; hash mismatches; malformed allocation data; cluster loops;
out-of-range clusters; invalid reserved-cluster use; inconsistent sizes; arithmetic overflow; and
unsupported versions. Unsafe corruption MUST NOT be silently mounted writable.

The persistent volume MUST address at least 50,000,000,000 bytes. A sparse 64 GiB or larger
reference disk MAY be used. Formatting, mounting, accounting, directory growth, allocation, and I/O
MUST operate at or above the minimum capacity.

**Rationale**: File data and defining companion metadata form one durability contract.

### VII. Extension-Hidden User and Application Views

Ordinary File Explorer views, ordinary CUI listings, and application-facing enumeration MUST NOT
reveal raw extensions or hashes. They MUST present extension-hidden names and MAY present icons
selected by the operating system from authoritative metadata.

Explicit kernel-owned CUI or GUI diagnostics MAY reveal extension, algorithm, stored hash,
companion version, validation status, and primary association. Companions MUST never appear as
separate user files. Search and type selection MAY use hashes as prefilters, but the authoritative
extension MUST be verified before returning or classifying a result. A collision MUST NOT merge,
hide, replace, or misclassify files.

**Rationale**: Normal presentation hides implementation metadata while explicit diagnostics keep
the filesystem experiment observable.

### VIII. Application and Shell Mediation

For the demonstrator, File Explorer and applications MUST request file enumeration and operations
through operating-system services exposed by the shell-facing system-call layer. That layer MUST
mediate kernel/VFS services; it MUST NOT implement an alternate filesystem path or expose raw
directory records.

File Explorer MUST receive display-safe entries and type/icon identifiers without extensions or
hashes. A proprietary application MUST receive only files matching types registered for that
application, represented by opaque handles and permitted metadata.

Custom applications MUST NOT query hidden extensions or hashes, claim arbitrary hidden types, or
bypass operating-system type selection. For a proprietary format, custom applications MUST use the
proprietary application's documented official API or another approved interoperability contract.
InferenceOS MUST verify authoritative extension identity internally before returning content.

A future production system MAY replace shell mediation with a formal native API only through an
approved specification preserving the VFS, metadata-hiding, authorization, and collision-safety
guarantees.

**Rationale**: Central mediation lets applications trust OS type selection without learning or
manipulating hidden metadata.

### IX. Research-Gated Extension Registry

InferenceOS-FS MAY contain an optional Extension Registry Block. When enabled, it MUST contain at
most one logical entry per canonical extension. Saving another file with the same extension MUST
refresh that entry rather than create a duplicate type entry.

The registry MAY optimize type enumeration and search, but MUST NOT be authoritative for identity,
extension equality, health, or correctness. Correct behavior MUST continue when it is disabled,
missing, stale, corrupt, or full. It MUST be rebuildable from authoritative file metadata.

Claims of saved branches, cycles, or time are hypotheses. Default enablement requires reproducible
comparison of instructions, branches, cycles, and user-visible latency against a disabled baseline.
Without demonstrated benefit and unchanged correctness, it MUST remain disabled by default.

**Rationale**: Experimental optimization must be measurable and unable to weaken correctness.

### X. Layered GUI and Shared Input

The GUI MUST follow this dependency direction:

```text
Applications / desktop shell
            |
 Widgets / terminal / file browser
            |
 Window manager or compositor
            |
 2D graphics and text rendering
            |
 Framebuffer / graphics abstraction
            |
 Supported QEMU graphics device
```

Keyboard and pointer drivers MUST feed a shared input-event abstraction consumed by CUI and GUI.
The GUI MUST provide pixel-addressable output, basic drawing, raster text, pointer rendering,
keyboard and pointer events, minimal window management, a terminal, a file browser, and a desktop.

Specifications and plans MUST define the graphics abstraction, pixel model, primitives, font,
input events, pointer semantics, window ownership and z-order, repaint behavior, desktop duties,
terminal integration, File Explorer VFS integration, and recovery. InferenceOS-FS MUST NOT depend
on GUI state or graphical types.

**Rationale**: Layering adds graphics without coupling storage to presentation state.

### XI. Freestanding C17 and Reproducible Builds

All project-owned source MUST reside beneath `src`. Kernel, VFS, filesystem, block, input, CUI,
GUI, rendering, windowing, command, and support code MUST use freestanding ISO C17 except for
narrowly approved GCC/Clang extensions and minimum required x86-64 assembly.

GCC and Clang MUST both be supported. Extensions MUST be source-controlled and SHOULD be isolated
behind portability abstractions. Assembly MUST be limited to CPU mechanisms not reasonably
expressible through approved C interfaces. Filesystem policy, allocation, hashing, parsing,
filename handling, GUI layout, rendering, windowing, and command semantics MUST NOT use assembly.

A clean checkout MUST produce a bootable QEMU image through one documented workflow. The project
MUST publish source, build scripts, filesystem and hash documentation, CUI/GUI documentation,
tests, a launch configuration, known limitations, and an approved open-source license.

**Rationale**: Freestanding C17, controlled extensions, and dual-compiler support keep low-level
behavior explicit and reproducible.

## Binding Technical Constraints

| Area | Binding requirement |
|---|---|
| Architecture / firmware | x86-64 / UEFI |
| Primary environment | QEMU |
| Kernel | Minimal modular monolithic |
| Source / language | `src` / freestanding ISO C17 |
| Compilers / assembly | GCC and Clang / minimal isolated x86-64 |
| Interfaces / recovery | Mandatory CUI and GUI; CUI usable without GUI |
| Storage boundary | One VFS namespace; InferenceOS-FS behind VFS and block layer |
| Allocation | FAT32-derived FAT and cluster model |
| Directory records | Distinct 32-byte primary and 32-byte companion records |
| Type authority | Exact extension; collision-safe verification |
| Ordinary presentation | Extensions and hashes hidden; type icons permitted |
| Application access | Shell-mediated demonstrator calls and OS-controlled selection |
| Extension registry | Optional, non-authoritative, benchmark-gated |
| Persistent volume | At least 50,000,000,000 bytes; sparse image permitted |
| Graphics / input | Layered pixel graphics; shared keyboard and pointer events |
| Deferred by default | Networking, multi-user security, POSIX compatibility, dynamic linking, package management, journaling, snapshots, encryption, compression, automatic repair, and FAT32 interoperability for the InferenceOS-FS root volume |

## Development Workflow and Quality Gates

Every specification and plan MUST include a Constitution Check proving, where applicable:

- mandatory CUI, GUI, and independent CUI recovery remain intact;
- all ordinary access shares one VFS namespace and InferenceOS-FS stays behind it;
- primary and companion records remain distinct and collision-safe;
- ordinary views and applications do not receive extensions or hashes;
- shell and application mediation cannot bypass the VFS;
- save ordering preserves file health;
- the registry remains optional, non-authoritative, and benchmark-gated;
- minimum filesystem capacity remains supported;
- GUI/input dependency directions remain intact;
- freestanding C17, extension allowlisting, dual compilers, and minimal assembly remain enforced;
  and
- release claims remain limited to tested capabilities.

Before an on-disk change is implemented, its specification MUST define offsets, widths, byte
order, version/capacity implications, create/save/rename/delete/interruption/recovery behavior, and
tests. Before a foundational GUI contract is implemented, its specification MUST define every
graphics, input, windowing, desktop, terminal, browser, and recovery boundary listed in Principle X.

Validation MUST cover CUI boot; GUI startup and failure recovery; keyboard and pointer input;
drawing and windowing; terminal and browser; cross-interface namespace coherence; format/mount and
minimum-capacity geometry; file lifecycle; companion validation; extension-changing rename;
collision safety; malformed metadata/allocation; reboot persistence; write/flush failures;
application metadata boundaries; registry-disabled correctness and enabled benchmarks; and GCC and
Clang builds.

Specifications MUST define user value, testable behavior, scope, assumptions, edge cases,
measurable outcomes, and a Constitution Check. Plans MUST resolve unknowns, record decisions and
alternatives, define concrete source structure and contracts, and re-check the constitution after
design. Tasks MUST be dependency ordered, independently verifiable, mapped to requirements or user
stories, and include required validation.

## Governance

This constitution is the highest governing artifact for InferenceOS. Specifications, plans, tasks,
implementation, tests, and release documentation MUST comply. When another artifact conflicts,
this constitution controls until an amendment is approved.

An amendment MUST document its rationale, affected principles, migration impact, and dependent
artifact changes. Compliance MUST be reviewed for specifications, plans, tasks, and every change
affecting governed kernel, storage, metadata, application, CUI, GUI, compiler, or assembly behavior.

Versions follow semantic versioning:

- **MAJOR**: incompatible removal or redefinition of a core principle or purpose;
- **MINOR**: new principle or materially expanded compatible governance; and
- **PATCH**: non-semantic clarification or correction.

Implementation choices not bound here—including exact signatures, geometry, algorithms, device
models, pixel formats, fonts, events, compositor policy, controller, and reference-disk size—MUST
be selected in approved specifications and plans.

**Version**: 2.1.0 | **Ratified**: 2026-08-08 | **Last Amended**: 2026-08-23
