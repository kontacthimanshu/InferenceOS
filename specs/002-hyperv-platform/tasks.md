# Tasks: Hyper-V Platform Support

**Input**: Design documents from `/specs/002-hyperv-platform/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

## Phase 1: Setup

**Purpose**: Register the new source/test/package surfaces without changing runtime behavior.

- [X] T001 Add Hyper-V source, include, hosted-test, and image target scaffolding in CMakeLists.txt and tests/CMakeLists.txt
- [X] T002 [P] Add Hyper-V protocol provenance and experimental-scope documentation in docs/hyperv.md and docs/limitations.md
- [X] T003 [P] Add Hyper-V image artifact names and output exclusions in .gitignore and docs/build.md

---

## Phase 2: Foundational Platform and Wire Contracts

**Purpose**: Build the shared platform selection and safe protocol primitives required by every user story.

- [X] T004 Add CPUID/MSR/Hyper-V architecture primitives and isolated hypercall entry in src/arch/x86_64/hyperv.c, src/arch/x86_64/hypercall.S, and src/arch/x86_64/include/inferenceos/arch/hyperv.h
- [X] T005 Add immutable q35/Hyper-V platform selection and service contract in src/kernel/platform.c and src/kernel/include/inferenceos/platform.h
- [X] T006 [P] Add bounded VMBus wire layouts, GUID helpers, and static assertions in src/drivers/hyperv/include/inferenceos/drivers/hyperv/protocol.h and tests/contract/hyperv_wire_layout_test.c
- [X] T007 [P] Add validated circular-ring implementation and hosted tests in src/drivers/hyperv/ring.c, src/drivers/hyperv/include/inferenceos/drivers/hyperv/ring.h, and tests/unit/hyperv_ring_test.c
- [X] T008 Add hypercall-page and SynIC initialization in src/drivers/hyperv/hypercall.c, src/drivers/hyperv/synic.c, and src/drivers/hyperv/include/inferenceos/drivers/hyperv/hypervisor.h
- [ ] T009 Add VMBus negotiation, message validation, offers, GPADL, channel open/close, and bounded polling in src/drivers/hyperv/vmbus.c, src/drivers/hyperv/channel.c, and src/drivers/hyperv/include/inferenceos/drivers/hyperv/vmbus.h
- [ ] T010 Add hosted VMBus negotiation/channel/fault tests in tests/integration/hyperv_vmbus_test.c and tests/fault/hyperv_channel_fault_test.c

**Checkpoint**: Architecture detection, codecs, rings, and channel state machines build and pass hosted tests.

---

## Phase 3: User Story 1 — Boot a Usable Recovery Console (Priority: P1) 🎯 MVP

**Goal**: Reach a keyboard-driven CUI in Hyper-V while preserving the q35 path.

**Independent Test**: Boot the Generation 2 VM and run `version` and `devices` through VMConnect.

- [ ] T011 [P] [US1] Refactor reusable scan-code translation from PS/2 into src/gui/input/scancode.c and src/gui/input/include/inferenceos/gui/scancode.h with regression coverage in tests/unit/ps2_input_test.c
- [X] T012 [P] [US1] Add synthetic keyboard protocol negotiation/event translation in src/drivers/hyperv/keyboard.c and src/drivers/hyperv/include/inferenceos/drivers/hyperv/keyboard.h
- [ ] T013 [P] [US1] Add Hyper-V detection and keyboard codec tests in tests/unit/hyperv_detection_test.c and tests/unit/hyperv_keyboard_test.c
- [X] T014 [US1] Route runtime input initialization/polling through platform services while preserving q35 behavior in src/kernel/runtime.c and src/block/platform_q35.c
- [ ] T015 [US1] Add Hyper-V/q35 platform contract and degraded-pointer tests in tests/contract/hyperv_platform_contract_test.c

**Checkpoint**: Hosted tests pass and a Hyper-V VM can reach and use the standalone CUI.

---

## Phase 4: User Story 2 — Use an InferenceOS-FS Data VHDX (Priority: P2)

**Goal**: Safely expose, format, mount, flush, and persist a separate data VHDX.

**Independent Test**: Format/mount the eligible 64 GiB disk, create a file, reboot, and read it back;
repeat after swapping boot/data SCSI locations.

- [X] T016 [P] [US2] Extend the UEFI boot handoff with GPT boot-partition identity in src/boot/uefi/uefi.h, src/boot/uefi/main.c, src/kernel/include/inferenceos/boot_info.h, and src/kernel/boot_info.c
- [X] T017 [P] [US2] Add fail-closed MBR/GPT/ESP/blank/InferenceOS-FS block classification in src/block/classify.c and src/block/include/inferenceos/block_classify.h
- [X] T018 [P] [US2] Add disk capability and classification tests including zero-write assertions in tests/unit/boot_disk_classification_test.c and tests/fault/boot_disk_classification_fault_test.c
- [X] T019 [P] [US2] Add StorVSC/SCSI wire codecs and completion/error mapping in src/drivers/hyperv/storvsc_protocol.c and src/drivers/hyperv/include/inferenceos/drivers/hyperv/storvsc.h
- [ ] T020 [US2] Implement synchronous StorVSC negotiation, LUN probing, READ/WRITE, flush, and reset/drain in src/drivers/hyperv/storvsc.c
- [X] T021 [US2] Select and publish only an eligible data LUN through the generic block interface in src/drivers/hyperv/storvsc.c, src/kernel/platform.c, and src/block/include/inferenceos/block.h
- [ ] T022 [P] [US2] Add StorVSC codec, I/O, flush, timeout, and error tests in tests/integration/hyperv_storvsc_test.c and tests/fault/hyperv_storage_fault_test.c
- [X] T023 [US2] Enforce format/root capabilities in CUI and mount paths in src/cui/fs_commands.c and src/filesystems/inferenceos_fs/mount.c with regression tests in tests/integration/fs_commands_test.c

**Checkpoint**: Boot VHDX is never writable/format-capable; data VHDX persists across reboot.

---

## Phase 5: User Story 3 — Use the Graphical Desktop (Priority: P3)

**Goal**: Qualify graphics and provide synthetic pointer input without weakening CUI recovery.

**Independent Test**: Start `gui`, operate terminal/File Explorer with keyboard and pointer, and
verify CUI recovery when display or pointer setup is faulted.

- [ ] T024 [P] [US3] Generalize GOP handoff validation for Hyper-V-supported direct RGB/BGR modes in src/boot/uefi/main.c, src/kernel/boot_info.c, and src/drivers/framebuffer/gop.c
- [ ] T025 [P] [US3] Add bounded HID report-descriptor parser in src/drivers/hyperv/hid.c and src/drivers/hyperv/include/inferenceos/drivers/hyperv/hid.h
- [X] T026 [US3] Add SynthHID mouse negotiation, report translation, and platform polling in src/drivers/hyperv/mouse.c and src/kernel/platform.c
- [ ] T027 [P] [US3] Add GOP, HID descriptor, mouse report, and graphical-fallback tests in tests/unit/hyperv_hid_test.c and tests/integration/hyperv_graphics_input_test.c

**Checkpoint**: GUI works when GOP qualifies; malformed HID/GOP degrades without losing CUI.

---

## Phase 6: User Story 4 — Restart and Shut Down Reliably (Priority: P4)

**Goal**: Preserve synchronization and flush guarantees before Hyper-V lifecycle transitions.

**Independent Test**: Repeat reboot/shutdown with clean and injected-failure storage states and verify
that only successful sync/flush permits the VM transition.

- [X] T028 [P] [US4] Extend UEFI boot information with validated runtime ResetSystem handoff in src/boot/uefi/uefi.h, src/boot/uefi/main.c, src/kernel/include/inferenceos/boot_info.h, and src/kernel/boot_info.c
- [X] T029 [US4] Implement Hyper-V UEFI runtime reboot/shutdown behind platform services in src/drivers/hyperv/power.c and src/kernel/platform.c
- [X] T030 [P] [US4] Add runtime-reset ABI and power-ordering tests in tests/integration/hyperv_power_test.c and tests/unit/power_test.c

**Checkpoint**: Successful transitions follow flush; failures leave the VM running and report error.

---

## Phase 7: Packaging, System Validation, and Documentation

**Purpose**: Produce attachable VHDXs, automate configuration checks, and retain evidence without
claiming qualification prematurely.

- [ ] T031 [P] Add deterministic boot/data VHDX builders and manifests in tools/image/build_hyperv_boot_vhdx.ps1 and tools/image/create_hyperv_data_vhdx.ps1
- [X] T032 [P] Add Hyper-V VM/profile preflight and evidence runner in tools/test/run_hyperv_tests.ps1 and tests/system/hyperv_profile_test.ps1
- [X] T033 Add CMake Hyper-V image and validation targets while preserving existing targets in CMakeLists.txt
- [X] T034 [P] Update operator documentation and experimental limitations in docs/build.md, docs/hyperv.md, docs/limitations.md, and README.md
- [X] T035 Run source/dependency validation, GCC/Clang hosted and cross builds, QEMU regression matrix, and available Hyper-V dry-run/profile tests; record results in specs/002-hyperv-platform/quickstart.md

---

## Dependencies and Execution Order

- Phase 1 precedes all implementation.
- Phase 2 is foundational and blocks all user stories.
- US1 blocks Hyper-V interactive system validation.
- US2 depends on VMBus but is otherwise independent of US3.
- US3 depends on US1 input/platform foundations and may proceed alongside late US2 work.
- US4 depends on the platform facade and US2 flush semantics.
- Packaging/system validation follows all required story implementations.

## Parallel Opportunities

- T002 and T003 can run together.
- T006 and T007 can run together after T001.
- T011–T013 can run together after VMBus contracts stabilize.
- T016–T019 can run together before StorVSC integration.
- T024 and T025 can run together; T027 follows their interfaces.
- T028 and T030 test preparation can begin alongside T029.
- T031, T032, and T034 can run together after artifact contracts stabilize.

## Implementation Strategy

1. Deliver and validate US1 as the MVP: platform detection, VMBus, and keyboard-driven CUI.
2. Add US2 storage with boot-medium safety before any destructive Hyper-V workflow.
3. Add US3 pointer/display qualification while preserving CUI fallback.
4. Add US4 lifecycle behavior and complete packaging/validation.
