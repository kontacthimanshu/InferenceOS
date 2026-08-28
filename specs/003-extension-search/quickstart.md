# Quickstart: Validate Extension Search

## Prerequisites

- Configure and build the GCC debug preset.
- Boot the rebuilt image with an InferenceOS-FS data disk mounted at `/`.

## CUI Scenario

```text
mkdir /WORK
mkdir /WORK/OLD
create /ROOT.DOC
create /WORK/REPORT.DOC
create /WORK/OLD/ARCHIVE.DOC
create /WORK/NOTES.TXT
search doc
```

Expected list:

```text
/ROOT
/WORK/REPORT
/WORK/OLD/ARCHIVE
```

Traversal order is deterministic but callers must not depend on sorting. The output must not contain `.DOC`, `stored_hash`, `recomputed_hash`, or an eight-digit hash value.

Repeat with `search .doc`, `search DOC`, and `search .DoC`; the locations must be identical.

## Empty and Invalid Queries

- `search ZIP` prints `no matches` when no ZIP file exists.
- `search`, `search .`, `search TOOLONG`, `search D.OC`, and `search DOC extra` print command usage through the existing invalid-argument path.

## Automated Validation

From WSL in a configured checkout, build and test the hosted presets, then build
both freestanding images:

```sh
cmake --build --preset gcc-host-debug
ctest --test-dir build/gcc-host-debug --output-on-failure
cmake --build --preset clang-host-debug
ctest --test-dir build/clang-host-debug --output-on-failure
cmake --build --preset gcc-debug
cmake --build --preset clang-debug
```

Validation on 2026-08-28 passed all 66 hosted tests with both GCC and Clang,
including `extension-search-service-contract`, `file-service-integration`, and
`fs-commands-integration`. Both freestanding debug images also built.

The dedicated QEMU action `extension_search_audit` was attempted against a
fresh persistent disk. It booted to `INFERENCEOS:CUI_READY`, accepted the test
control action, and formatted `disk0`, but timed out while executing the
repository's existing `mount disk0 /` path. The existing
`format_mount_remount` action stops at the same mount/reinitialization path, so
the boot-level list-rendering assertion remains blocked by that pre-existing
mount hang rather than by the extension-search command. The retained audit log
for this run is under
`build/qemu-tests/extension-search-audit-20260828T130239892Z-9044/serial.log`.
