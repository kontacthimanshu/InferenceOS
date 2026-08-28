# Implementation Plan: Hyper-V Platform Support

**Branch**: `[002-hyperv-platform]` | **Date**: 2026-08-27 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/002-hyperv-platform/spec.md`

## Summary

Add Hyper-V Generation 2 as an experimental secondary runtime while retaining QEMU/q35 as the
primary reference. A boot-selected platform service layer will keep kernel runtime code independent
of device models. The Hyper-V implementation will establish a bounded VMBus client, expose one
eligible synthetic-SCSI LUN through the existing block contract, translate synthetic keyboard/HID
pointer messages into normalized input events, qualify the retained UEFI GOP framebuffer, and use
the retained UEFI runtime reset service for reboot and shutdown. Loader-provided GPT partition
identity plus fail-closed disk-content classification will prevent the FAT32 boot VHDX from becoming
an InferenceOS-FS format or root target.

## Requirements Traceability

| Spec reference | Design responsibility | Contract / artifact | Validation approach |
|---|---|---|---|
| US1, FR-001–FR-006 | Platform selection, hypercall/SynIC/VMBus, keyboard | `contracts/platform-services.md`, `contracts/vmbus.md` | Hosted protocol tests plus Hyper-V cold-boot/CUI matrix |
| US2, FR-007–FR-012 | StorVSC, LUN discovery, disk classification | `contracts/storage.md`, `data-model.md` | Codec/fault tests, disk-order permutations, format/mount/persistence matrix |
| US3, FR-005, FR-013–FR-014 | Synthetic HID pointer and GOP qualification | `contracts/input-display.md` | Hosted HID tests and VMConnect GUI/fallback scenarios |
| US4, FR-015 | Durable power transition and runtime reset | `contracts/input-display.md` | Power ordering unit tests and repeated reboot/shutdown trials |
| FR-016–FR-021 | Dependency boundaries, builds, packaging, claims | `quickstart.md` | Source audit, GCC/Clang builds, QEMU regression, Hyper-V evidence manifest |

## Technical Context

**Language/Version**: Freestanding ISO C17 with narrowly isolated x86-64 assembly for the Hyper-V
hypercall entry and existing context/interrupt mechanisms

**Primary Dependencies**: Pinned GCC 16.2.0 and Clang/LLD 22.1.8; UEFI; Microsoft Hyper-V TLFS;
Microsoft OpenVMM protocol definitions as an MIT-licensed reference; existing runtime, physical and
virtual memory, interrupt, input, block, power, VFS, and InferenceOS-FS libraries

**Storage**: Separate FAT32 boot VHDX and unpartitioned 64 GiB or larger data VHDX; synchronous
StorVSC primary-channel requests; whole-disk InferenceOS-FS version 1 unchanged

**Testing**: Hosted unit, contract, integration, and fault-injection tests for all wire codecs and
state machines; dual-compiler cross builds; existing QEMU matrix; manual/automated Hyper-V
Generation 2 boot, input, disk-order, persistence, GUI/fallback, and power matrix

**Target Platform**: x86-64 UEFI; QEMU 11.1.0/q35 remains primary; Hyper-V Generation 2 on current
Windows 11 Pro is the experimental secondary qualification target

**Project Type**: Freestanding monolithic operating-system demonstrator

**Performance Goals**: Reach CUI within 30 seconds; maintain one outstanding synchronous storage
request initially; complete every protocol wait within a documented bounded timeout; no boot-disk
writes; retain current QEMU behavior

**Constraints**: One vCPU initially; static memory at least 512 MiB; Secure Boot disabled; no dynamic
memory, save/restore, live migration, networking, or hot-remove guarantee; 512-byte logical sectors;
minimum 50,000,000,000-byte data device; no InferenceOS-FS disk-format change

**Scale/Scope**: One primary VMBus synthetic-SCSI controller, LUNs 0–63, one selected data/root LUN,
one synthetic keyboard, one synthetic HID pointer, retained firmware framebuffer, guest-initiated
reboot/shutdown; VMBus subchannels and tagged/concurrent SCSI are deferred

## Constitution Check

*GATE: Must pass before Phase 0 research and again after Phase 1 design.*

### Pre-Research Gate

- **CUI and GUI recovery**: PASS — platform input feeds the shared queue and graphics failure leaves
  the CUI usable.
- **VFS/storage boundary**: PASS — StorVSC implements only the generic block contract.
- **InferenceOS-FS identity and durability**: PASS — the format is unchanged and flush maps to a
  completed SCSI synchronization command.
- **Metadata hiding and application mediation**: PASS — no platform path reaches directory records,
  VFS callers, Shell, or applications.
- **Input and graphics layering**: PASS — new drivers remain below normalized input and framebuffer
  abstractions.
- **Freestanding and reproducible builds**: PASS — C17, dual compilers, controlled assembly, and the
  QEMU primary workflow remain mandatory.
- **Release claims**: PASS — Hyper-V remains experimental until its complete matrix passes.

### Post-Design Gate

- **CUI and GUI recovery**: PASS — `contracts/platform-services.md` makes keyboard mandatory and
  pointer/display degradable.
- **VFS/storage boundary**: PASS — `contracts/storage.md` terminates every Hyper-V dependency at
  `ios_block_device`.
- **InferenceOS-FS identity and durability**: PASS — the separate ESP/root topology and
  SYNCHRONIZE CACHE completion rule are explicit.
- **Boot-medium safety**: PASS — loader identity, GPT/ESP classification, and fail-closed format
  capability are defined in `data-model.md` and `contracts/storage.md`.
- **Input and graphics layering**: PASS — `contracts/input-display.md` preserves normalized events
  and the retained GOP abstraction.
- **Freestanding and reproducible builds**: PASS — new source and validation locations are concrete,
  no general-purpose runtime dependency is introduced, and QEMU regressions are required.

## Project Structure

### Documentation (this feature)

```text
specs/002-hyperv-platform/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- platform-services.md
|   |-- vmbus.md
|   |-- storage.md
|   `-- input-display.md
`-- tasks.md
```

### Source Code (repository root)

```text
src/
|-- arch/x86_64/
|   |-- hyperv.c
|   |-- hypercall.S
|   `-- include/inferenceos/arch/hyperv.h
|-- boot/uefi/
|-- kernel/
|   |-- platform.c
|   |-- runtime.c
|   `-- include/inferenceos/platform.h
|-- drivers/hyperv/
|   |-- hypercall.c
|   |-- synic.c
|   |-- vmbus.c
|   |-- ring.c
|   |-- channel.c
|   |-- storvsc.c
|   |-- keyboard.c
|   |-- mouse.c
|   |-- hid.c
|   |-- power.c
|   `-- include/inferenceos/drivers/hyperv/*.h
|-- block/
|   |-- device.c
|   |-- classify.c
|   `-- platform_q35.c
|-- drivers/ps2/
|-- drivers/virtio_blk/
|-- gui/input/
|-- drivers/framebuffer/
|-- vfs/
`-- filesystems/inferenceos_fs/

tools/image/
|-- build_hyperv_boot_vhdx.ps1
`-- create_hyperv_data_vhdx.ps1

tools/test/
`-- run_hyperv_tests.ps1

tests/
|-- unit/
|-- contract/
|-- integration/
|-- fault/
`-- system/
```

**Structure Decision**: Hyper-V architecture primitives stay under `arch/x86_64`; protocol/device
drivers stay under `drivers/hyperv`; the kernel platform facade owns boot-time selection and runtime
polling; storage classification belongs to the generic block layer. Existing q35, PS/2, virtio-blk,
VFS, filesystem, Shell, and GUI boundaries remain intact. Existing dirty edits in overlapping files
must be preserved and integrated rather than replaced.

## Phase 0 Research Decisions

- **Decision**: Detect Hyper-V through architectural CPUID leaves and use a boot-selected platform
  facade; separate kernels and probe-all behavior were rejected.
- **Decision**: Implement a single-vCPU VMBus client with SynIC pages, bounded polling, interrupt
  notification, contiguous rings, and validated codecs; multi-vCPU/subchannels are deferred.
- **Decision**: Use a synchronous, one-request StorVSC driver with SCSI INQUIRY, READ CAPACITY,
  READ/WRITE(10), and SYNCHRONIZE CACHE(10); a general SCSI mid-layer is unnecessary.
- **Decision**: Make synthetic keyboard mandatory, parse mouse HID descriptors, and retain GOP as
  the first Hyper-V display path; hardcoded mouse reports and unqualified graphics are rejected.
- **Decision**: Pass UEFI runtime reset information and GPT boot-partition identity to the kernel;
  q35 power ports, disk order, and capacity-only selection are unsafe on Hyper-V.
- **Decision**: Derive wire facts from Microsoft TLFS/OpenVMM and independently implement codecs;
  Linux remains a behavior cross-check rather than a source-code dependency.

## Phase 1 Design Outputs

- **Data model**: Platform, channel, ring, storage LUN, boot identity, input, display, and power state
  with fail-closed validation and transitions in [data-model.md](data-model.md).
- **Contracts**: Platform selection, VMBus transport, StorVSC/block semantics, and input/display/power
  contracts under [contracts](contracts/).
- **Quickstart validation**: End-to-end build, image preparation, VM configuration, and evidence
  workflow in [quickstart.md](quickstart.md).
- **Post-design constitution result**: PASS — no exception or amendment is required.

## Complexity Tracking

No constitutional violations require justification.

