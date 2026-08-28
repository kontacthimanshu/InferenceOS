# Quickstart Validation: InferenceOS

This guide defines the runnable validation contract implementation must provide. It does not imply
that the greenfield repository already contains these commands.

## Prerequisites

- CMake and Ninja
- x86_64 ELF GCC/binutils cross-toolchain
- Clang and LLD with `x86_64-none-elf` support
- QEMU x86-64 and OVMF firmware
- PowerShell 7 on Windows or the documented equivalent wrapper on other hosts
- At least enough host space for sparse images and retained failure artifacts

On Windows, run the idempotent checked-in bootstrap described by T001:

```powershell
pwsh tools/bootstrap/wsl-ubuntu.ps1
```

On native Ubuntu, run the checked-in Linux entry point instead:

```bash
./tools/bootstrap/wsl-ubuntu.sh
```

The wrapper invokes `tools/bootstrap/wsl-ubuntu.sh`, reads pinned versions from
`tools/bootstrap/versions.json`, validates existing installations before changing them, and emits a
sourceable project environment file without modifying a user's global shell profile.
The authoritative version matrix is fixed by R2 in `research.md`; the bootstrap must report and
reject any silent version substitution.

After bootstrapping, run every command below inside Ubuntu (native or WSL), from the repository
root, after sourcing the generated environment. On a default WSL installation:

```bash
source "$HOME/.local/share/inferenceos/tools/environment.sh"
./tools/bootstrap/wsl-ubuntu.sh --check
```

Do not reuse a build directory between Windows and WSL because CMake caches contain host-specific
absolute paths.

## 1. Configure and Validate Both Compilers

```bash
cmake --preset gcc-debug
cmake --build --preset gcc-debug
cmake --preset gcc-host-debug
cmake --build --preset gcc-host-debug
ctest --preset gcc-host

cmake --preset clang-debug
cmake --build --preset clang-debug
cmake --preset clang-host-debug
cmake --build --preset clang-host-debug
ctest --preset clang-host
```

Expected: every project-owned C translation unit compiles as freestanding C17; unit/property tests
pass; fixed on-disk size/offset assertions compile under both profiles; the extension allowlist has
no undeclared use.

## 2. Build Reference Images

```bash
cmake --build --preset gcc-debug --target inferenceos-image
cmake --build --preset gcc-debug --target inferenceos-test-disk
```

Expected outputs under the build directory:

- ESP image containing `EFI/BOOT/BOOTX64.EFI` and the ELF64 kernel;
- versioned ESP system-module manifest plus separately hashed static ELF64 Shell/GUI application images;
- sparse raw persistent disk of 64 GiB logical capacity;
- symbols/map artifacts and a reproducible QEMU launch manifest.

## 3. Run Fast Filesystem Integration Tests

```bash
ctest --preset gcc-integration
```

Expected: formatter geometry, VFS lifecycle, 8.3 canonicalization, CRC/hash vectors, companion
association, collision verification, registry fallback, mount outcomes, and every specified
corruption class pass against memory and sparse-file block backends.

## 4. Boot to CUI and Start GUI

```bash
cmake --build --preset gcc-debug --target test-boot
cmake --build --preset gcc-debug --target test-gui
```

Expected: 20 fresh boots reach the stable CUI-ready marker; 20 GUI starts reach GUI-ready; injected
GOP/font/pointer initialization failures leave the CUI responsive. Keyboard and pointer injection,
window movement, GUI terminal, and File Explorer checkpoints pass.

## 5. Validate Shared Namespace and Persistence

The automated serial transcript MUST perform operations equivalent to:

```text
format <persistent-device>
mount <persistent-device> /
mkdir /DOCS
create /DOCS/REPORT.TXT
write /DOCS/REPORT.TXT "persistent data"
sync
hashinfo /DOCS/REPORT.TXT
gui
shutdown
```

Run that persistent-disk scenario, including 20 reboot cycles, with:

```bash
cmake --build --preset gcc-debug --target test-reboot-persistence
```

After a fresh boot using the same persistent disk, it MUST remount, read the expected bytes from
both the standalone CUI and GUI terminal, demonstrate that a GUI-terminal mutation is visible from
the standalone CUI, show `REPORT` without `.TXT` in ordinary CUI and File Explorer views, show the
correct icon, and expose the extension/hash only in explicitly authorized diagnostics. See
[filesystem contract](contracts/inferenceos-fs.md) and
[application contract](contracts/shell-application.md).

## 6. Run Fault and Corruption Matrix

```bash
ctest --preset gcc-fault
```

Expected: failures injected at data, FAT, primary, companion, registry, write, and flush phases
never produce false durable success. Reboot observes a valid committed pair or a detectable
incomplete/corrupt state. Unsafe structures select diagnostic read-only or rejected mount and never
cause out-of-volume access.

## 7. Validate Application Metadata Boundaries

```bash
ctest --preset gcc-contract
```

Expected: ordinary DTOs contain zero extensions and hashes; proprietary test application receives
only trusted types; custom test application cannot enumerate arbitrary hidden types; forged/stale
handles fail; diagnostic authority remains separate.

## 8. Run Registry Research Gate

```bash
cmake --build --preset gcc-debug --target benchmark-registry-qemu
```

Expected: matched enabled/disabled runs report corpus seed/checksum, build and QEMU versions,
instructions, conditional branches, median/p95 latency, spread, optional separately identified
hardware cycles, and durable-save overhead. Registry remains default-off unless the predefined gate
in [research.md](research.md#r12--extension-registry-benchmark-gate) passes with no correctness
difference.

## Evidence Retention

On failure, retain compiler identity, build manifest, QEMU command, serial transcript, framebuffer
checkpoint, disk image or mutation recipe, fault rule, and random seed. Release evidence MUST cover
all SC-001–SC-020 outcomes and both post-design constitutional validation paths.
