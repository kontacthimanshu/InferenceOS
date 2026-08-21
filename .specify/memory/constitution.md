<!--
Sync Impact Report
- Version change: N/A → 1.0.0
- Project scope: Minimal bootable operating-system demonstrator
- Primary architectural objective:
  - Demonstrate a VFS backed by InferenceFS-FAT32
  - Demonstrate an additional directory entry containing a deterministic hash
    of each file's extension
- Added principles:
  - I. Minimal Demonstration Scope
  - II. Character-Prompt Operating Environment
  - III. Mandatory VFS Boundary
  - IV. InferenceFS-FAT32 Allocation Model
  - V. Extension-Hash Directory Entry (NON-NEGOTIABLE)
  - VI. Persistent Filesystem Integrity
  - VII. Observable Filesystem Demonstration
  - VIII. Simplicity and Reproducibility
- Intentionally deferred:
  - User-mode applications
  - Multitasking
  - Multi-user security
  - Networking
  - Graphics
  - Long filenames
  - Standard FAT32 interoperability for the InferenceFS-FAT32 root volume
  - Journaling
  - Automatic filesystem repair
-->

# InferenceOS Minimal InferenceFS-FAT32 Demonstration Constitution

## Core Principles

### I. Minimal Demonstration Scope

InferenceOS Minimal MUST be a small, bootable, open-source operating-system
demonstrator whose primary purpose is to prove the operation of
**InferenceFS-FAT32**.

The system MUST demonstrate:

1. booting a real kernel;
2. presenting an interactive character command prompt;
3. accessing a persistent block device;
4. exposing files through a Virtual Filesystem (VFS);
5. mounting InferenceFS-FAT32 through the VFS;
6. creating, reading, writing, listing, renaming, and deleting files;
7. storing an additional InferenceFS-FAT32 directory entry for each file;
8. storing a deterministic hash of the file extension in that additional entry;
9. inspecting the primary directory entry and extension-hash entry from the
   command prompt; and
10. retaining successfully flushed files and their extension-hash metadata
    across reboot.

Features not required to demonstrate these capabilities MUST be deferred from
the first usable release.

The project MUST be described as a filesystem and operating-system proof of
concept. It MUST NOT be represented as a production-ready or general-purpose
operating system.

**Rationale**: The goal is to demonstrate one filesystem idea clearly rather
than reproduce the breadth of a modern operating system.

---

### II. Character-Prompt Operating Environment

The first usable release MUST target:

* x86-64;
* UEFI;
* QEMU;
* one virtual CPU;
* one documented persistent virtual block device; and
* a character-oriented command prompt.

The system MUST boot to a prompt similar in purpose to:

```text
InferenceOS>
```

The command prompt MAY execute directly in kernel mode.

A user-mode process architecture, ELF loader, `fork`, `exec`, scheduler,
multiple processes, process isolation, and multi-user environment are NOT
required for the first release.

The boot path MUST initialize only the functionality necessary for:

1. CPU startup;
2. memory management;
3. exception handling;
4. console output;
5. keyboard input;
6. the selected block device;
7. the VFS;
8. InferenceFS-FAT32; and
9. the command interpreter.

**Rationale**: A command interpreter running directly over a small kernel is
sufficient to demonstrate persistent filesystem behavior.

---

### III. Mandatory VFS Boundary

InferenceOS Minimal MUST contain a Virtual Filesystem abstraction.

The command interpreter and filesystem-independent kernel components MUST
access persistent files through the VFS.

The required architecture MUST preserve this dependency direction:

```text
Character command prompt
        |
        v
       VFS
        |
        v
InferenceFS-FAT32
        |
        v
Generic block-device interface
        |
        v
QEMU storage driver
        |
        v
Virtual disk
```

The VFS MUST provide at least:

* mount;
* unmount;
* create;
* open;
* close;
* read;
* write;
* seek where required;
* list directory;
* rename;
* remove;
* create directory;
* query metadata; and
* flush.

Generic callers MUST NOT directly manipulate:

* FAT entries;
* cluster numbers;
* raw directory entries;
* extension-hash records;
* filesystem boot structures; or
* physical disk sectors.

InferenceFS-FAT32 MUST implement the VFS filesystem contract.

Only one persistent filesystem implementation is required for the first
release.

**Rationale**: The VFS proves that InferenceFS-FAT32 is a filesystem
implementation behind a generic operating-system file interface rather than
being hard-coded into every command.

---

### IV. InferenceFS-FAT32 Allocation Model

InferenceFS-FAT32 MUST be a distinct filesystem derived from selected FAT32
concepts.

It MAY reuse:

* reserved filesystem sectors;
* FAT-style allocation tables;
* 32-bit FAT entries with defined usable cluster-number bits;
* free-cluster values;
* bad-cluster values;
* reserved values;
* end-of-chain values;
* cluster chains;
* cluster-based directories;
* redundant FAT copies;
* an FSInfo-like free-space hint; and
* FAT-style first-cluster references.

InferenceFS-FAT32 MUST NOT be advertised as standards-conforming FAT32.

The root InferenceFS-FAT32 volume MUST have:

* a unique filesystem signature;
* a filesystem format version;
* documented logical-sector requirements;
* documented sectors-per-cluster rules;
* documented FAT layout;
* documented directory-entry rules; and
* explicit mount-time validation.

If UEFI requires a standard FAT filesystem for booting, the disk MAY contain:

```text
Disk
|
+-- EFI System Partition
|      |
|      +-- Standard FAT32
|              |
|              +-- InferenceOS boot files
|
+-- InferenceOS Root Partition
       |
       +-- InferenceFS-FAT32
```

Standard FAT32 used for the EFI System Partition MUST remain logically separate
from InferenceFS-FAT32.

**Rationale**: Reusing FAT32 allocation concepts minimizes implementation work
while allowing the project to demonstrate a deliberate filesystem extension.

---

### V. Extension-Hash Directory Entry (NON-NEGOTIABLE)

Every regular file stored in InferenceFS-FAT32 MUST have:

1. one primary FAT32-derived directory entry; and
2. one associated InferenceFS-FAT32 extension-hash directory entry.

Both records MUST occupy exactly 32 bytes.

A logical directory entry set therefore conceptually contains:

```text
+----------------------------------+
| Extension-Hash Entry   32 bytes  |
+----------------------------------+
| Primary File Entry     32 bytes  |
+----------------------------------+
```

The two records together consume 64 bytes of directory storage, but they MUST
remain two independently identifiable 32-byte directory records.

The primary file entry MUST contain the ordinary file information required by
the initial filesystem specification, including:

* short filename;
* short extension;
* attributes;
* first cluster;
* file size; and
* required timestamps or reserved fields.

The companion extension-hash entry MUST contain at least:

* an InferenceFS-FAT32 entry-type identifier;
* an entry-format version;
* a hash-algorithm identifier;
* the stored extension hash;
* sufficient information to associate the entry with its primary file entry;
* integrity information or an entry-set checksum; and
* reserved bytes for future compatible evolution.

The exact byte offsets MUST be defined in the InferenceFS-FAT32 format
specification.

The extension-hash record MUST be immediately associated with its primary
entry according to one deterministic directory-entry-set rule.

A conforming implementation MUST never guess which primary entry belongs to an
extension-hash entry.

#### Extension Input

For the first minimal release, filenames MAY be restricted to the native
short-name model.

The file extension MUST therefore be derived from the extension portion of the
short filename.

For example:

```text
REPORT.TXT
```

logically contains:

```text
Base name: REPORT
Extension: TXT
```

The extension hash MUST be calculated from a canonical representation of:

```text
TXT
```

and NOT from:

```text
REPORT.TXT
```

unless a later constitutional amendment changes the hashing contract.

The separator dot MUST NOT participate in the extension hash.

#### Canonicalization

The hash contract MUST define a deterministic canonicalization procedure before
hashing.

The first filesystem specification MUST explicitly define:

* whether extension bytes are converted to upper or lower case;
* how trailing padding is removed;
* how an empty extension is represented;
* whether extension bytes are interpreted as ASCII or another encoding;
* the hash algorithm;
* hash width;
* byte order used when storing the hash; and
* the algorithm identifier written to the companion record.

Two files having extensions considered equivalent by the filename-comparison
rules MUST produce the same canonical extension input.

#### Hash Meaning

The extension hash MUST be treated as derived metadata.

It MUST NOT be treated as:

* the filename itself;
* proof of file type;
* cryptographic proof of file contents;
* a security credential; or
* a globally unique identifier.

Hash collisions MUST be considered possible.

Whenever correctness depends on the actual extension, the implementation MUST
verify the original extension after using the hash as an index, filter, lookup
accelerator, classification hint, or other derived value.

**Rationale**: The extension-hash companion record is the defining experimental
feature of this filesystem and must be explicit, deterministic, observable,
and safe in the presence of collisions.

---

### VI. Persistent Filesystem Integrity

A successfully completed and flushed file operation MUST preserve both:

* the primary file directory entry; and
* its associated extension-hash entry.

The filesystem MUST NOT expose a file as completely committed when its required
extension-hash entry is absent, invalid, or only partially committed.

File creation MUST logically commit:

```text
allocate required directory records
        |
        v
create extension-hash metadata
        |
        v
create primary file metadata
        |
        v
allocate file clusters as required
        |
        v
flush according to documented ordering
        |
        v
mark operation successfully committed
```

The exact physical write sequence MAY differ, but the filesystem specification
MUST define crash-consistency behavior.

Deletion MUST invalidate or release both records belonging to the file.

Rename MUST recompute the extension hash whenever the file extension changes.

For example:

```text
REPORT.TXT
```

renamed to:

```text
REPORT.LOG
```

MUST cause the extension-hash metadata to change from the canonical hash of:

```text
TXT
```

to the canonical hash of:

```text
LOG
```

A rename that changes only the base filename and leaves the extension unchanged
SHOULD preserve the same extension hash.

The filesystem MUST detect at least:

* an extension-hash entry without a primary entry;
* a primary file entry without its mandatory extension-hash entry;
* an unsupported hash-entry version;
* an unsupported hash-algorithm identifier;
* an invalid entry-set checksum;
* an extension hash inconsistent with the actual stored extension;
* duplicate or ambiguous companion entries;
* malformed cluster chains;
* cluster-chain loops;
* out-of-range clusters;
* bad or reserved clusters used incorrectly;
* file size inconsistent with allocated storage; and
* unsupported filesystem versions.

Detected inconsistencies MUST be reported rather than silently ignored.

**Rationale**: The experiment is meaningful only when the additional metadata
survives normal filesystem operations and remains reliably associated with its
file.

---

### VII. Observable Filesystem Demonstration

The command prompt MUST make the InferenceFS-FAT32 experiment directly
observable.

The first release MUST provide commands equivalent in purpose to:

```text
help
version
clear

devices
diskinfo

format
mount
unmount
fsinfo
sync

dir
cd
pwd
mkdir
rmdir

create
write
append
type
rename
delete

fileinfo
hashinfo
fatinfo

reboot
shutdown
```

Exact command names MAY differ.

A command equivalent to `hashinfo` MUST allow a user to select a file and
display at least:

```text
File name
Extension
Canonical extension input
Hash algorithm
Extension hash
Hash-entry version
Hash-entry location
Associated primary-entry location
Hash validation result
```

For example:

```text
InferenceOS> hashinfo REPORT.TXT

File                 : REPORT.TXT
Extension            : TXT
Canonical extension  : TXT
Hash algorithm       : <configured algorithm>
Extension hash       : <stored value>
Hash entry version   : 1
Hash validation      : VALID
```

A command equivalent to `fileinfo` MUST expose enough information to show the
relationship:

```text
Directory entry set
|
+-- Extension hash entry
|      +-- extension hash
|      +-- algorithm/version
|      +-- association information
|
+-- Primary file entry
       +-- filename
       +-- extension
       +-- first cluster
       +-- file size
```

A command equivalent to `fatinfo` MUST allow observation of the file's cluster
chain.

**Rationale**: An experimental filesystem should demonstrate its distinctive
behavior without requiring an external debugger or hex editor.

---

### VIII. Simplicity and Reproducibility

The implementation MUST prefer the smallest understandable mechanism that
satisfies the required demonstration.

The first release MAY use:

* a single CPU;
* a single kernel execution context;
* synchronous filesystem calls;
* a simple physical-memory allocator;
* a simple kernel heap;
* a simple block cache;
* polling device I/O where acceptable;
* one mounted persistent filesystem; and
* built-in kernel commands.

The first release MUST NOT require:

* general user-mode processes;
* multitasking;
* `fork`;
* `exec`;
* dynamic linking;
* shared libraries;
* multiple users;
* authentication;
* ACLs;
* networking;
* sockets;
* USB;
* audio;
* graphical output;
* mouse input;
* multiple CPUs;
* POSIX compatibility;
* package management;
* journaling;
* snapshots;
* compression;
* encryption;
* symbolic links;
* hard links; or
* automatic repair of every corruption condition.

Kernel code MUST use freestanding ISO C17 plus the minimum required
architecture-specific assembly.

A clean repository checkout MUST produce a bootable QEMU image using one
documented build workflow.

The project MUST publish:

* source code;
* build scripts;
* InferenceFS-FAT32 format documentation;
* extension-hash format documentation;
* command documentation;
* test cases;
* filesystem test images where appropriate; and
* known limitations

under an approved open-source license.

**Rationale**: The value of the project comes from the clarity of the
filesystem experiment, not the number of conventional OS features implemented.

---

## Binding Technical Constraints

The first usable release MUST conform to the following baseline:

| Area                    | Requirement                                     |
| ----------------------- | ----------------------------------------------- |
| Architecture            | x86-64                                          |
| Firmware                | UEFI                                            |
| Primary environment     | QEMU                                            |
| Kernel                  | Minimal modular monolithic kernel               |
| Interface               | Character command prompt                        |
| VFS                     | Mandatory                                       |
| Persistent filesystem   | InferenceFS-FAT32                               |
| Allocation model        | FAT32-derived cluster/FAT model                 |
| Primary directory entry | 32 bytes                                        |
| Extension-hash entry    | Additional 32-byte record                       |
| Extension hash          | Mandatory for every regular file                |
| Hash algorithm          | Fixed and versioned by filesystem specification |
| Hash collision handling | Original extension MUST remain authoritative    |
| Long filenames          | Deferred                                        |
| Root filesystem         | InferenceFS-FAT32                               |
| EFI System Partition    | Standard FAT32 when required                    |
| Storage                 | One documented QEMU block-device implementation |
| Language                | Freestanding ISO C17 plus x86-64 assembly       |
| Networking              | Deferred                                        |
| Graphics                | Deferred                                        |
| User-mode processes     | Deferred                                        |
| Multitasking            | Deferred                                        |
| Multi-user security     | Deferred                                        |

---

## Mandatory Demonstration Scenario

The first release MUST support the following complete demonstration:

1. boot InferenceOS under QEMU;
2. reach the character command prompt;
3. identify the persistent virtual disk;
4. format it as InferenceFS-FAT32;
5. mount it through the VFS;
6. create `TEST.TXT`;
7. write known content to the file;
8. inspect its primary directory entry;
9. inspect its extension-hash entry;
10. verify that the hash corresponds to canonical `TXT`;
11. create `SECOND.TXT`;
12. demonstrate that both files produce the same extension hash;
13. create `THIRD.LOG`;
14. demonstrate that `LOG` produces a different hash under the selected
    algorithm unless a genuine hash collision occurs;
15. rename `TEST.TXT` to `TEST.LOG`;
16. verify that the extension hash is recomputed;
17. flush the filesystem;
18. reboot the operating system;
19. remount InferenceFS-FAT32;
20. verify file data and extension-hash metadata;
21. delete a file;
22. verify that both its primary and extension-hash entries are released or
    marked deleted according to the format; and
23. shut down safely.

A genuine hash collision MUST NOT cause one filename to replace, hide, or become
indistinguishable from another.

---

## Development Workflow and Quality Gates

Every feature specification and implementation plan MUST include a
**Constitution Check**.

The check MUST verify that:

* the feature contributes to the minimal filesystem demonstration;
* access to files remains mediated by the VFS;
* InferenceFS-FAT32 remains behind the VFS;
* primary and hash directory entries remain distinct;
* the extension hash remains derived metadata;
* extension text remains authoritative over its hash;
* hash collisions are handled safely;
* persistent updates preserve the primary/hash-entry relationship;
* the command prompt remains the primary interface; and
* deferred operating-system functionality has not silently become mandatory.

Implementation MUST NOT begin for a filesystem-format change until the
specification defines:

1. affected on-disk structures;
2. byte offsets;
3. integer sizes;
4. byte order;
5. version implications;
6. creation behavior;
7. rename behavior;
8. deletion behavior;
9. crash/interruption behavior; and
10. required test cases.

Build-time assertions MUST validate all fixed on-disk structure sizes and
offsets.

The test suite MUST cover at least:

* boot to command prompt;
* VFS mounting;
* file creation;
* file reading and writing;
* directory operations;
* empty extension;
* one-character extension;
* two-character extension;
* three-character extension;
* files sharing the same extension;
* files having different extensions;
* extension-changing rename;
* base-name-only rename;
* extension-hash verification;
* intentionally corrupted hash metadata;
* unsupported hash algorithm;
* missing companion entry;
* orphaned companion entry;
* duplicate companion entry;
* hash collision handling;
* fragmented file cluster chains;
* persistence after reboot;
* full-volume behavior; and
* malformed FAT/cluster structures.

---

## Governance

This Constitution is the highest governing artifact for the
InferenceOS Minimal InferenceFS-FAT32 Demonstration project.

Specifications, plans, tasks, implementation, tests, and release documentation
MUST comply with it.

When another project artifact conflicts with this Constitution, this
Constitution takes precedence.

The following changes require a constitutional amendment:

* replacing the VFS with direct filesystem access;
* replacing InferenceFS-FAT32 as the demonstration filesystem;
* removing the additional extension-hash entry;
* merging the hash into the primary directory entry instead of using a
  companion entry;
* changing the principle that the original extension is authoritative;
* making a hash collision equivalent to filename equality;
* materially expanding the project from a filesystem demonstrator into a
  general-purpose operating system; or
* changing the mandatory character-prompt interaction model.

Detailed choices including:

* exact filesystem signature;
* FAT geometry;
* primary-directory-entry layout;
* extension-hash-entry byte layout;
* canonicalization algorithm;
* hash algorithm;
* hash width;
* hash byte order;
* checksum algorithm;
* entry association mechanism;
* storage controller; and
* individual command syntax

MUST be defined by approved specifications and MAY evolve without a
constitutional amendment when the change preserves every constitutional
principle and uses appropriate on-disk format versioning.

Constitution versions follow semantic versioning:

* **MAJOR**: incompatible change to a core principle or filesystem experiment;
* **MINOR**: new mandatory principle or materially expanded scope;
* **PATCH**: clarification that does not change required behavior.

Every pull request affecting the VFS, block layer, InferenceFS-FAT32,
directory-entry structure, hashing behavior, command interface, or persistent
write behavior MUST identify the applicable constitutional principle and tests.

**Version**: 1.0.0
**Ratified**: 2026-08-08
**Last Amended**: 2026-08-08
