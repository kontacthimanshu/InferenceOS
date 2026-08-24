# Contract: Shell, File Explorer, and Applications

## Mediation

Kernel starts one trusted Shell service. Clients send versioned IPC requests for directory views,
type/icon views, safe search, trusted-application enumeration, content-handle acquisition, GUI view
requests, and approved proprietary-adapter invocation. Shell validates request kind and caller
identity, invokes kernel/VFS services, and returns bounded replies. It never parses raw filesystem
records or grants direct storage access.

## Ordinary Reply Schema

Permitted fields are opaque object handle, extension-free display name, object kind, size, generic
attributes, opaque type/icon capability, and allowed operations. Extension bytes/length, hash and
algorithm, canonical internal name, companion/cluster/FAT/registry details are forbidden.

Proprietary enumeration is authorized by immutable kernel application identity and trusted binding,
not caller-supplied type data. Custom applications cannot query or select arbitrary hidden types and
use approved proprietary adapters through opaque content handles. Fabricated, stale, wrong-kind,
or cross-process handles return stable errors and grant no rights.

## Diagnostics

Diagnostic requests use a separate kernel-minted capability and separate DTOs. Diagnostic authority
does not change ordinary DTO schemas or application rights. Hash-based routing always verifies the
authoritative primary extension internally.
