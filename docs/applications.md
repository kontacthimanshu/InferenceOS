# InferenceOS Application Contracts

InferenceOS applications use versioned kernel, IPC, Shell, handle, and capability contracts. They
do not receive raw filesystem structures or select files by supplying hidden extensions or hashes.
The first demonstrator uses the trusted Shell as the broker between graphical/application clients
and kernel/VFS services.

This is an interface-isolation demonstrator, not a production security model or a general
third-party application platform.

## Process and module model

The runtime contract assigns each system application its own statically linked ELF64 image,
ring-3 address space, user and kernel stacks, immutable kernel-assigned application identity,
process-local handle table, and IPC endpoints. Initial modules are supplied from the ESP with a
versioned manifest containing identity, role, path, length, entry ABI, required/optional state, and
SHA-256 digest.

The intended startup order is Shell, GUI desktop/terminal, File Explorer, then optional test
applications. Shell is required. Invalid GUI modules degrade to the CUI; an unavailable optional
File Explorer disables that application without changing filesystem correctness.

Handles are 64-bit process-local slot/generation values. The kernel validates object kind and
rights on every resolution. Closing a handle advances its generation, so stale, fabricated,
wrong-kind, or cross-process handles grant no authority.

The repository currently compiles and host-tests these components but does not yet link the final
kernel or standalone application ELFs. This document defines the implemented interfaces without
claiming a completed clean-checkout boot; see [build instructions](build.md#package-reference-images)
and [limitations](limitations.md).

## Shell mediation

The kernel registers one trusted Shell service. IPC supplies the caller's process and application
identity; a client cannot assert either value in the operation payload. Every request has a
nonzero request ID, exact size, ABI version, zero flags/reserved fields, bounded payload, and a
known operation.

| Operation | Value | Request contract |
|---|---:|---|
| `DIRECTORY_VIEW` | 1 | Opaque directory handle; type capability must be zero. |
| `TYPE_VIEW` | 2 | Opaque directory handle and a valid opaque type capability. |
| `SEARCH` | 3 | Same capability requirement as type view; results still require authoritative exact-type verification. |
| `GUI_VIEW` | 4 | Opaque window/view handles and a strictly increasing render sequence. |

File-view replies contain at most four entries per IPC message and use a continuation token for
paging. A reply echoes versioned framing and an operation status. Shell validates every returned
entry before passing it to a client. GUI-view replies echo the render sequence.

Restarting the Shell unregisters the old service generation and invalidates outstanding channels.
File Explorer detects a generation change, reconnects, and may retry the exchange once; callers
must not treat an old channel or handle as transferable authority.

## Display-safe entry contract

The ordinary wire DTO is fixed at version 1 and contains only:

- opaque object handle;
- extension-free display name;
- regular-file or directory kind;
- byte size and generic read-only attribute;
- opaque type/icon capability; and
- allowed open, read, write, rename, delete, or enumerate operations.

The following data is forbidden from ordinary replies:

- canonical internal 8.3 name or extension bytes/length;
- extension hash, hash algorithm, or companion state;
- primary/companion record or FAT/cluster locations;
- registry contents; and
- raw filesystem or block addresses.

Display names are bounded to 63 bytes plus a terminator. Hidden-name collisions are represented by
stable presentation labels such as `REPORT` and `REPORT (2)`; those labels do not rename the
underlying files. Companions cannot be represented as ordinary entries.

Directory enumeration flows through Shell to the kernel file-view service and then VFS. Type and
search operations may use internal hash information only as a prefilter; authoritative extension
bytes must match before an object is returned. Registry-enabled and registry-disabled paths must
produce equivalent correct visible results.

## Type catalog and application bindings

The kernel owns the mapping from internal file-type identities to presentation icons. The exposed
type/icon value is an opaque boot-generation capability, not an extension or hash. Unknown mappings
resolve to a generic-file icon.

A separate trusted binding registry associates an immutable application identity with the internal
types it is allowed to handle. Minting a process type capability requires both a valid catalog
capability and a matching trusted application binding. The capability is scoped to that process
and application, carries enumerate rights only, and cannot be guessed or reused by another process.

Caller-provided extensions, hashes, type identities, or invented opaque values never expand the
binding registry.

## Proprietary application contract

A supported proprietary application asks Shell for files using its trusted application identity
and a kernel-minted type capability. Shell returns only authoritative exact-type matches as
display-safe entries with read-only content handles and permitted generic metadata.

The reference proprietary test application verifies that each result:

- is a version-1 regular-file DTO;
- has an extension-free display name;
- grants only open/read operations;
- contains a nonzero opaque type capability; and
- resolves to a process-local readable content handle.

Hash collisions are resolved internally against the authoritative primary extension before a file
is returned. A type capability is routing authority for the bound application only; it is not a
security label, filename, or proof of file content.

The examples demonstrate the routing model. They do not claim that Microsoft Word, Excel, PDF, or
other proprietary products or formats have been ported.

## Custom application and approved-adapter contract

An ordinary custom application cannot enumerate files by arbitrary hidden extension/hash or claim
a native type. Requests with nonzero reserved fields, extension-like flags, or fabricated type
capabilities fail deterministically.

Operations requiring proprietary-format knowledge use an explicitly approved adapter:

1. trusted boot/configuration policy registers an adapter identity, proprietary application,
   authorized caller identity, allowed operation mask, and required content rights;
2. the authorized custom process receives a process-local adapter handle;
3. the caller supplies that adapter handle, an opaque content handle, an allowed operation number,
   and at most 128 bytes of versioned payload;
4. the service validates caller identity, handle ownership, operation mask, content kind/rights,
   sizes, flags, and reserved fields;
5. it grants the proprietary implementation a reduced content handle containing only the required
   rights; and
6. it returns a bounded versioned reply and closes temporary authority.

Registration is never accepted as a caller assertion. Absent adapters return an explicit error,
and InferenceOS does not invent, reverse engineer, or label unofficial integrations as official
APIs.

## GUI rendering contract

Applications do not render by sending raw framebuffer addresses. A GUI-view request contains
opaque window and view handles. The kernel resolves both through the caller's handle table,
requires write access to the window and read access to the view, checks that both identify the same
owner-bound target, and rejects duplicate or decreasing render sequences. Only then may the window
manager compose that owned window.

This keeps graphical ownership separate from file-view metadata and prevents a client from using a
render request to acquire another process's window.

## Diagnostics are separate authority

Filesystem diagnostics use a kernel-minted diagnostic handle, separate request/reply DTOs, and
explicit filesystem, record, allocation, or registry scopes. That path may reveal canonical names,
extensions, hashes, records, and chains after bounded validation. Possessing diagnostic authority
does not add those fields to ordinary Shell replies or widen proprietary/custom application rights.

See [CUI diagnostics](cui.md#privileged-diagnostic-commands) and the GUI diagnostic inspector in
[GUI architecture](gui.md#file-explorer).

## ABI and failure rules

- Versioned structures begin with 16-bit `size` and `version` fields.
- Reserved fields and unsupported flags must be zero.
- Counts, continuation tokens, and payload lengths are bounded before access.
- User pointers are copied through validated kernel copy-in/copy-out paths rather than trusted.
- Unknown operations or versions and malformed replies fail with stable `IOS_E_*` status values.
- Process exit closes handles, cancels waits, releases IPC/address-space resources, and records a
  collectable status.
- Version 1 has no fork, dynamic linking, multicore scheduling, user-selected priority, or package
  ecosystem.

## Implementation and validation references

- [`shell_protocol.h`](../src/shell/include/inferenceos/shell_protocol.h) defines Shell operations
  and fixed wire layouts; [`service.c`](../src/shell/service.c) validates and dispatches them.
- [`display_safe_entry.h`](../src/shell/include/inferenceos/display_safe_entry.h) defines the
  ordinary metadata boundary.
- [`application_bindings.h`](../src/kernel/include/inferenceos/application_bindings.h) and
  [`type_capability.h`](../src/kernel/include/inferenceos/type_capability.h) define trusted type
  routing.
- [`proprietary_service.h`](../src/shell/include/inferenceos/proprietary_service.h) and
  [`proprietary_adapter.h`](../src/shell/include/inferenceos/proprietary_adapter.h) define
  proprietary enumeration and approved adapters.
- [`proprietary_test`](../src/applications/proprietary_test/) and
  [`custom_test`](../src/applications/custom_test/) exercise the two application models.
- Contract and integration tests under [`tests/contract`](../tests/contract/) and
  [`tests/integration`](../tests/integration/) validate metadata hiding, handles, Shell mediation,
  File Explorer, routing, adapters, and diagnostics.

The concise normative boundary is
[`shell-application.md`](../specs/001-inferenceos/contracts/shell-application.md).
