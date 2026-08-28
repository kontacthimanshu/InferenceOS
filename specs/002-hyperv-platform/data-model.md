# Data Model: Hyper-V Platform Support

## Platform Identity

Fields: kind (`q35` or `hyperv`), detected vendor/interface, capability bits, selected services,
initialization stage, last error. State: `unknown -> detected -> initializing -> ready|failed`.
Selection is immutable after device initialization.

## Boot Medium Identity

Fields: validity, partition scheme, 16-byte GPT partition signature, source-device-path status.
Validation requires a GPT-signature device-path node. Missing or malformed identity does not make a
disk eligible; content classification remains fail-closed.

## VMBus Connection

Fields: negotiated version, connection ID, message/event pages, SynIC vector, monitor pages, offer
catalog, next GPADL and transaction IDs, state, last error. State:
`disconnected -> synic_ready -> negotiating -> connected -> disconnecting|failed`.

## VMBus Channel

Fields: class and instance GUIDs, child relation ID, monitor group/bit, target VP, connection ID,
offer bytes, GPADL handle, outbound/inbound rings, callback, state. State:
`offered -> gpadl_pending -> opening -> open -> rescinded|closed|failed`.

## Ring

Fields: shared header, data start, data size, producer/consumer indices, pending-send size,
interrupt mask, ownership. Every transition validates indices and aligned packet lengths before
copying. Host-owned indices are untrusted.

## Synthetic Storage Controller

Fields: channel, offered path/target, negotiated protocol, maximum transfer, bounce buffer, active
transaction, LUN catalog, frozen/failed status. Only one request may be active initially.

## Synthetic Storage LUN

Fields: path, target, LUN, capacity sectors, logical-sector size, inquiry identity, media state,
classification, format/root capabilities, block-device publication state. Classification:
`unclassified -> boot_protected|partitioned_protected|blank_eligible|inferenceos_eligible|unsupported`.
Any read or validation failure transitions to `unsupported` with no destructive capability.

## Storage Transaction

Fields: monotonically allocated nonzero ID, operation, direction, LBA/count, requested/actual bytes,
bounce pages, deadline, completion statuses, state. State:
`free -> prepared -> submitted -> completed|timed_out -> reset_pending -> drained|failed`.

## Synthetic Keyboard

Fields: channel, negotiated version, modifier/caps state, key-down set, event counters, state.
State: `offered -> negotiating -> ready|failed`. Valid events feed the shared input queue.

## Synthetic HID Pointer

Fields: channel, protocol version, device information, validated HID/report descriptors, parsed
button/X/Y/wheel fields, state. State:
`offered -> negotiating -> descriptor_pending -> ready|unsupported|failed`.

## Display Qualification

Fields: base, size, width, height, stride, pixel format/masks, retained-memory status, qualification
result. State: `unexamined -> qualified|unavailable`. Unavailable display cannot fail CUI startup.

## Power Transition

Fields: requested action, synchronization result, block-flush result, runtime-service address,
platform result. State follows the existing controller:
`ready -> flushing -> transitioning`; any pre-transition failure returns to `ready`.

