# Research: Hyper-V Platform Support

## Sources and Licensing Boundary

- Microsoft Hyper-V TLFS is authoritative for CPUID, synthetic MSRs, hypercalls, SynIC, messages,
  events, and architectural ordering.
- Microsoft OpenVMM is the preferred device-protocol reference because it is Microsoft-maintained
  and MIT licensed.
- UEFI 2.10 is authoritative for GOP, device paths, GPT, loaded-image identity, and ResetSystem.
- Upstream Linux documentation and drivers are behavior cross-checks only. No implementation code is
  copied from GPL sources.

Primary references:

- https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/tlfs
- https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/hypercalls/hvcallpostmessage
- https://github.com/microsoft/openvmm
- https://github.com/microsoft/openvmm/blob/main/vm/devices/storage/storvsp_protocol/src/lib.rs
- https://github.com/torvalds/linux/blob/master/Documentation/virt/hyperv/vmbus.rst
- https://uefi.org/specs/UEFI/2.10/

## Decision 1: Platform Detection and Selection

**Decision**: Detect `Microsoft Hv` through CPUID hypervisor leaves, validate the required feature
bits, and select one immutable platform-services instance before input or storage initialization.

**Rationale**: Hyper-V Gen2 omits PS/2, PCI, and q35 devices. Explicit selection prevents fatal
legacy probes and preserves QEMU behavior.

**Alternatives considered**: Separate kernels increase packaging complexity; probe-all risks I/O on
absent hardware and ambiguous failures.

## Decision 2: VMBus Substrate

**Decision**: Allocate and identity-map the hypercall, SynIC message/event, monitor, and channel ring
pages from the kernel physical allocator. Configure one SynIC vector, negotiate VMBus, request
offers, and open only recognized class GUIDs. Validate every shared message and bound every wait.

**Rationale**: Storage and input share VMBus. A narrow single-processor substrate is sufficient and
minimizes concurrent state.

**Alternatives considered**: UEFI boot protocols end at ExitBootServices; multi-vCPU affinity and
subchannels add no initial value.

## Decision 3: Storage Protocol and Failure Semantics

**Decision**: Bind synthetic SCSI `ba6163d9-04a1-4d29-b605-72e2ffb1dc7f`, negotiate StorVSC 6.2,
6.0, then 5.1, and issue one synchronous request at a time. Probe LUNs 0–63; implement INQUIRY,
READ CAPACITY, READ/WRITE(10), and SYNCHRONIZE CACHE(10) through pinned GPA-direct buffers.

Require matching completion, successful outer/SRB/SCSI status, and exact transfer length. Timeout
freezes submission and reset/drains before resource reuse; timed-out writes are never blindly retried.

**Rationale**: The synchronous block API preserves ordering naturally. Write completion is not a
durability barrier; completed SYNCHRONIZE CACHE is.

**Alternatives considered**: Tagged queues, subchannels, discard, optical media, and NVMe/vPCI are
deferred.

## Decision 4: Boot-Disk Exclusion

**Decision**: Copy the GPT partition signature from the loader device path into boot information.
Validate protective MBR, primary/backup GPT and CRCs; protect the containing disk and every disk with
an ESP. Classification failure is fail-closed. Admit only blank unpartitioned media or existing valid
InferenceOS-FS as a format/root candidate.

**Rationale**: Offer order, controller location, LUN, name, and capacity are not safety boundaries.

**Alternatives considered**: First disk, SCSI location zero, or capacity-only selection are unsafe.

## Decision 5: Input and Display

**Decision**: Negotiate synthetic keyboard 1.0 on class
`f912ad6d-2b17-48ea-bd65-f927a61c7684` and share scan-code translation with PS/2. Negotiate
SynthHID 2.0 on mouse class `cfa8b69e-5b4a-4cc0-b98b-8ba1a1f3f95a`, validate HID descriptors,
and parse supported mouse usages. Qualify retained GOP, reject PixelBltOnly, preserve physical stride
and range, and degrade to CUI if unsupported.

**Rationale**: Keyboard unlocks recovery; HID parsing avoids one-layout assumptions; GOP is the
smallest graphics path already compatible with existing abstractions.

**Alternatives considered**: PS/2 probing, hardcoded HID offsets, and immediate synthetic video.

## Decision 6: Power

**Decision**: Pass the UEFI ResetSystem runtime entry and preserve runtime mappings. After existing
sync/flush ordering succeeds, call ResetSystem for cold reset or shutdown using the Microsoft x64
ABI. Host-initiated integration-service shutdown is a follow-on after guest-initiated power works.

**Rationale**: Hyper-V Gen2 does not expose q35 reset/shutdown ports.

**Alternatives considered**: q35 ports and undocumented reset hypercalls.

