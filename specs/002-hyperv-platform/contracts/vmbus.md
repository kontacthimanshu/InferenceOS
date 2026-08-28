# Contract: Hyper-V and VMBus

## Architectural Initialization

1. Validate Hyper-V CPUID vendor, interface, feature, and privilege leaves.
2. Register a nonzero guest OS identity.
3. Allocate a page-aligned hypercall page and enable the hypercall MSR.
4. Allocate and clear SynIC message and event pages, program one synthetic interrupt, then enable
   SynIC.
5. Negotiate a supported VMBus version using bounded PostMessage retries.
6. Request offers and retain only recognized, well-formed device class/instance pairs.

## Channel Contract

Channel open establishes a GPADL for page-aligned outbound/inbound rings, submits an open request,
and requires a matching successful response. Channel close/rescind prevents new packets, drains or
fails active transactions, and only then releases shared pages.

## Packet Contract

- Packet descriptor is 16 bytes and all offsets/lengths are measured in 8-byte units.
- Every packet is 8-byte aligned and ends with the required previous-packet index trailer.
- Producer publishes bytes, executes a release barrier, publishes write index, and signals when the
  host is not masking notifications.
- Consumer acquires after observing write index, validates descriptor/payload/trailer, copies
  untrusted shared data, then publishes read index.
- Invalid indices, arithmetic overflow, unknown required message types, mismatched transactions,
  or out-of-bounds PFNs fail the affected channel.
- All waits are bounded and expose a stable diagnostic stage.

