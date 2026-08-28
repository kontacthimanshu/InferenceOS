# Feature Specification: Hyper-V Platform Support

**Feature Branch**: `[002-hyperv-platform]`

**Created**: 2026-08-27

**Status**: Draft

**Input**: User description: "Make InferenceOS compatible with a Hyper-V Generation 2 virtual machine by adding a Hyper-V platform backend, VMBus and synthetic interrupts, storage, input, display qualification, power handling, and boot/data-disk selection tests."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Boot a Usable Recovery Console (Priority: P1)

An operator can attach a boot VHDX to a Hyper-V Generation 2 virtual machine, start it with Secure
Boot disabled, and reach an independently usable InferenceOS character interface using the VM
console keyboard.

**Why this priority**: Boot and recovery input are prerequisites for every administrative and
storage workflow. They also provide the smallest independently useful Hyper-V port.

**Independent Test**: Start a one-processor, 512 MiB Generation 2 VM from the packaged boot VHDX,
enter `version` and `devices`, and observe valid replies without a boot failure or panic.

**Acceptance Scenarios**:

1. **Given** a correctly packaged boot VHDX and Secure Boot disabled, **When** the VM starts,
   **Then** InferenceOS reaches its CUI and accepts keyboard input through VMConnect.
2. **Given** graphics or pointer initialization is unavailable, **When** the VM starts, **Then**
   the CUI remains usable and reports the unavailable optional service without halting.
3. **Given** the same source checkout, **When** the QEMU reference image starts, **Then** its
   existing q35 boot and PS/2 input behavior remains unchanged.

---

### User Story 2 - Use an InferenceOS-FS Data VHDX (Priority: P2)

An operator can attach a separate blank data VHDX, identify it unambiguously in the CUI, format it
as InferenceOS-FS, mount it as the root filesystem, and retain files across a clean reboot.

**Why this priority**: Persistent storage is the central operating-system capability and the main
reason to run the demonstrator in a VM.

**Independent Test**: Attach one boot VHDX and one blank 64 GiB data VHDX, format and mount only the
data disk, create and synchronize a file, reboot, and verify the file contents.

**Acceptance Scenarios**:

1. **Given** separate boot and blank data VHDXs, **When** `devices` is run, **Then** the data disk is
   exposed as a format-capable device and the boot disk is never offered as that device.
2. **Given** a supported blank data VHDX, **When** it is formatted and mounted, **Then** the mounted
   volume is InferenceOS-FS rather than FAT32, NTFS, or the EFI System Partition.
3. **Given** a synchronized file on the mounted data VHDX, **When** InferenceOS reboots and remounts
   the disk, **Then** the file and its authoritative companion metadata remain valid.
4. **Given** a storage write, flush, protocol, or disconnect failure, **When** an operation is
   attempted, **Then** the failure is surfaced without reporting unpersisted data as committed.

---

### User Story 3 - Use the Graphical Desktop (Priority: P3)

An operator can start the graphical desktop in VMConnect, use keyboard and pointer input, and work
with the same mounted namespace through the terminal and File Explorer.

**Why this priority**: The GUI completes the demonstrator experience but depends on working boot,
input, storage, and the recovery CUI.

**Independent Test**: Start `gui`, create or rename a file through one interface, observe it through
the other, and return to the CUI after a deliberate GUI startup failure.

**Acceptance Scenarios**:

1. **Given** a supported Hyper-V display mode, **When** `gui` is entered, **Then** the desktop,
   terminal, pointer, and File Explorer render and accept input.
2. **Given** a mounted InferenceOS-FS volume, **When** a file changes through the GUI terminal or
   File Explorer, **Then** the other interface observes the same VFS namespace.
3. **Given** an unsupported display mode or failed graphical service, **When** GUI startup fails,
   **Then** the standalone CUI remains usable.

---

### User Story 4 - Restart and Shut Down Reliably (Priority: P4)

An operator can request reboot or shutdown from the CUI or GUI and have Hyper-V perform the
requested transition only after required filesystem synchronization completes.

**Why this priority**: Clean lifecycle handling protects persistence and makes repeated validation
practical after the core boot, storage, and interface paths work.

**Independent Test**: Create and synchronize data, issue reboot and shutdown separately, observe
the VM transition, and verify the data after the next start.

**Acceptance Scenarios**:

1. **Given** dirty persistent state, **When** reboot or shutdown is requested, **Then** storage is
   synchronized before the Hyper-V power transition.
2. **Given** a failed synchronization, **When** reboot or shutdown is requested, **Then** the
   transition is refused and the failure is reported.

### Edge Cases

- The VM has only the FAT32 boot VHDX and no eligible data VHDX.
- The boot and data VHDXs are attached in the opposite controller order.
- Multiple eligible data disks have equal capacity or duplicate device identifiers.
- A VMBus channel is rescinded or reset while an I/O request is outstanding.
- The data VHDX reports a logical sector size other than 512 bytes or less than the minimum capacity.
- A synthetic input event contains an unsupported key, button, coordinate, or malformed message.
- Hyper-V exposes no acceptable graphical mode, or the firmware framebuffer becomes unavailable.
- The VM is started under QEMU, on bare metal, or under an unknown hypervisor.
- The operator attempts to format the boot disk or a mounted disk.
- Reboot or shutdown is requested while a storage flush is incomplete.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST detect a supported Hyper-V Generation 2 environment without changing
  the behavior of the existing QEMU/q35 reference environment.
- **FR-002**: The system MUST select a platform implementation at boot and MUST fail with an explicit
  diagnostic when no supported platform can be initialized.
- **FR-003**: The system MUST reach an independently usable CUI in a supported Hyper-V VM.
- **FR-004**: The system MUST accept keyboard input from the Hyper-V VM console and feed it through
  the existing shared input-event abstraction.
- **FR-005**: The system MUST accept pointer input from the Hyper-V VM console when the GUI is active
  and MUST not make pointer availability a prerequisite for CUI recovery.
- **FR-006**: The system MUST enumerate Hyper-V synthetic devices and reject malformed, unsupported,
  or rescinded device channels without memory corruption or indefinite waits.
- **FR-007**: The system MUST expose the eligible Hyper-V data VHDX through the existing generic
  block-device contract with read, write, capacity, logical-sector-size, and flush behavior.
- **FR-008**: Storage completion and error outcomes MUST preserve the existing InferenceOS-FS save,
  synchronization, and failure-reporting guarantees.
- **FR-009**: The system MUST distinguish the FAT32 boot VHDX from InferenceOS-FS data disks using a
  deterministic rule that does not depend on controller attachment order alone.
- **FR-010**: The system MUST never expose the boot VHDX as a format-capable CUI device and MUST
  reject any attempt to select it as the InferenceOS-FS root volume.
- **FR-011**: An eligible data disk MUST use 512-byte logical sectors, provide at least
  50,000,000,000 bytes, and remain a whole-disk InferenceOS-FS volume separate from the EFI System
  Partition.
- **FR-012**: The system MUST preserve all version-1 InferenceOS-FS on-disk structures and directory
  semantics; this feature MUST NOT reinterpret the volume as standards-conforming FAT32.
- **FR-013**: The system MUST qualify a firmware-provided graphical framebuffer for Hyper-V or mark
  the GUI unavailable while retaining the CUI.
- **FR-014**: When graphics are qualified, the existing desktop, GUI terminal, and File Explorer
  MUST operate over the same VFS namespace as the CUI.
- **FR-015**: The system MUST synchronize required storage state before requesting Hyper-V reboot or
  shutdown and MUST refuse the transition when synchronization fails.
- **FR-016**: Hyper-V-specific drivers and platform code MUST remain below existing block, input,
  framebuffer, power, VFS, filesystem, Shell, and GUI abstractions.
- **FR-017**: Project-owned implementation code MUST remain freestanding ISO C17 with only approved,
  isolated x86-64 assembly or compiler extensions.
- **FR-018**: GCC and Clang builds and the existing QEMU validation profile MUST continue to pass.
- **FR-019**: Automated tests MUST cover environment detection, message and ring validation,
  storage read/write/flush and failures, input translation, boot-disk exclusion, graphical fallback,
  power ordering, and QEMU regression behavior.
- **FR-020**: The project MUST provide a deterministic Hyper-V boot/data image preparation workflow
  and document VM generation, memory, processor, Secure Boot, controller, and disk requirements.
- **FR-021**: Release documentation MUST label Hyper-V support experimental until the complete
  Hyper-V boot, persistence, input, GUI/fallback, and power validation matrix has passed.

### Key Entities

- **Platform identity**: The detected execution environment and the selected platform services.
- **Synthetic bus channel**: A validated Hyper-V device communication channel, including identity,
  negotiated version, ring state, connection state, and lifecycle.
- **Synthetic storage device**: A VHDX-backed block device with capacity, sector geometry, stable
  identity, eligibility, request state, and flush capability.
- **Boot-medium identity**: The firmware-derived identity of the device containing the EFI loader,
  used to exclude that device from destructive storage operations.
- **Synthetic input device**: A keyboard or pointer channel that produces normalized input events.
- **Display qualification**: The availability and validated geometry of the retained firmware
  framebuffer used by the existing graphics abstraction.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A documented Hyper-V Generation 2 configuration reaches a responsive CUI in 30 seconds
  or less on the qualification laptop in 20 consecutive cold boots.
- **SC-002**: In all automated and manual disk-order permutations, zero attempts expose or format the
  boot VHDX as an InferenceOS-FS data device.
- **SC-003**: A 64 GiB data VHDX completes format, mount, file lifecycle, synchronization, and 20
  reboot-persistence cycles without data or companion-metadata loss.
- **SC-004**: Injected storage, channel, and flush failures produce bounded error outcomes in 100% of
  prescribed cases and never report an incomplete save as committed.
- **SC-005**: Keyboard input supports every character needed by the documented CUI grammar, and
  pointer input completes the prescribed desktop and File Explorer workflow without lost actions.
- **SC-006**: GUI qualification succeeds when the required framebuffer is available; otherwise the
  CUI remains responsive in 100% of graphical-failure cases.
- **SC-007**: Reboot and shutdown each complete only after successful synchronization in 20 repeated
  lifecycle trials, while every injected synchronization failure prevents the transition.
- **SC-008**: All existing GCC, Clang, hosted, dependency-boundary, and QEMU reference tests pass with
  no reduction in their existing coverage or success criteria.
- **SC-009**: A user following the Hyper-V quickstart can create, attach, boot, format, and validate
  the prescribed VM disks without an undocumented step.

## Assumptions

- The target is a local Hyper-V Generation 2 VM on a current Windows 11 Pro or equivalent supported
  Hyper-V host, with one virtual processor and at least 512 MiB of static memory.
- Secure Boot is disabled because the project bootloader is not signed for Hyper-V firmware.
- The boot medium is a small FAT32 EFI System Partition in one VHDX; the root data medium is a
  separate, initially blank 64 GiB VHDX.
- Networking, time synchronization, clipboard integration, dynamic memory, checkpoints, live
  migration, and production Hyper-V integration services are outside this feature.
- Polling may be used during initial channel bring-up, but operations must remain bounded and the
  final design must preserve deterministic failure behavior.
- QEMU remains the constitutionally required primary reference environment; Hyper-V is an additional
  experimental platform until its validation matrix is complete.

## Constitution Check *(mandatory)*

- **I. Demonstrable Operating-System Scope**: PASS — the feature adds a tested environment without
  broadening production-readiness claims.
- **II. Shared CUI and GUI Environments**: PASS — the CUI remains the independent recovery path and
  both interfaces retain the same services and namespace.
- **III. VFS-Mediated Storage Boundary**: PASS — Hyper-V storage terminates at the generic block
  layer; VFS and filesystem consumers remain driver-independent.
- **IV. Versioned InferenceOS-FS Format**: PASS — the FAT32 ESP remains separate and the existing
  version-1 root-volume format is unchanged.
- **V. Authoritative Extension and Companion Hash**: PASS — no directory-record or companion
  contract changes are introduced.
- **VI. Durable Save and Filesystem Integrity**: PASS — synthetic storage must implement ordered
  write and flush outcomes without weakening failure handling.
- **VII. Extension-Hidden Views**: PASS — platform devices do not alter presentation metadata.
- **VIII. Application and Shell Mediation**: PASS — no new application storage path is introduced.
- **IX. Research-Gated Extension Registry**: PASS — registry behavior and defaults are unchanged.
- **X. Layered GUI and Shared Input**: PASS — synthetic input feeds the shared abstraction and
  display support remains behind the framebuffer layer.
- **XI. Freestanding C17 and Reproducible Builds**: PASS — the port preserves dual-compiler C17
  builds, controlled assembly, and the primary QEMU workflow.

