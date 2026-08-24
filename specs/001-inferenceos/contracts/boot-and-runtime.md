# Contract: Boot and Runtime Boundaries

## UEFI Handoff

The loader MUST validate ELF64 segments, obtain the final memory map, select 1024x768 BGRX8888 GOP,
and call `ExitBootServices` before entering the kernel. A versioned, size-prefixed boot structure
contains memory-map location/count/descriptor size, framebuffer address/size/stride/format, ESP
information needed only during boot, and a checksum. Invalid or unsupported input halts with a COM1
diagnostic; unavailable reference graphics boots the CUI and marks GUI unavailable.

Before `ExitBootServices`, the loader MUST parse `/InferenceOS/System/modules.manifest` and load each
required static ELF64 module from the ESP. Manifest records contain application identity, role,
normalized absolute ESP path, byte length, entry ABI version, and SHA-256 digest. Boot information
contains versioned descriptors with identity, role, immutable memory range, length, ABI, and digest.
The kernel revalidates every descriptor and digest before mapping modules into private processes.
Shell is required; invalid GUI desktop/terminal modules degrade to CUI with diagnostics, while an
absent optional File Explorer module disables only that application. Module memory MUST NOT overlap
the kernel, boot information, framebuffer, or unavailable memory-map regions.

## Syscall ABI

- `RAX`: syscall number/result; negative values are stable `-IOS_E_*` errors.
- Arguments: `RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9`; `RCX` and `R11` clobbered.
- Structures begin with 16-bit `size` and `version`; reserved fields are zero.
- Kernel validates canonical mapped ranges, overflow, direction, alignment, maximums, and overlap,
  then uses copy-in/copy-out rather than dereferencing user pointers.
- `SYS_ABI_INFO` returns major/minor and feature bits. Unknown calls, versions, flags, or nonzero
  reserved data fail deterministically.

## Process and Scheduler Contract

Each system application is a separate statically linked ELF64 ring-3 image with private page tables,
kernel/user stacks, immutable kernel-assigned identity, handle table, and IPC endpoints. A
single-core local-APIC-timer scheduler preempts equal-priority ring-3 processes round-robin and runs
fixed-priority kernel work plus an idle thread. Processes block without polling on IPC, input,
timers, and explicit waits. Exit closes handles, cancels waits, releases address-space resources,
and records a collectable status. Version 1 exposes no fork, dynamic linking, multicore scheduling,
or user-controlled priorities. A non-yielding GUI/application MUST NOT prevent CUI recovery work.

The kernel discovers the local APIC from ACPI MADT, masks/remaps the legacy PIC, calibrates against
the ACPI PM timer, and applies a 10 ms ring-3 quantum. Storage/input/CUI recovery kernel work has
fixed priority above ring-3 work and MUST block when idle. Interrupt handlers queue deferred work.

## CUI

Both consoles use one parser/registry. Input is printable ASCII, Backspace, Enter, maximum 255-byte
payload, whitespace tokens, double quotes, and escaped quote/backslash. Version 1 has no scripting,
pipes, redirection, expansion, or globbing. Errors are `error: <symbol>: <message>` and return to the
prompt. `sync`, `reboot`, and `shutdown` succeed only after required flush completion.
