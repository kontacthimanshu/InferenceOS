# Quickstart Validation Guide

This is the runnable interface the implementation must provide; `$speckit-tasks` will decompose its construction.

## Prerequisites

- GCC 16.2 `x86_64-elf` and GNU binutils 2.45
- Clang/LLVM 22.1.8
- CMake 4.1+, Ninja 1.12+, Python 3.12+
- QEMU 11.1.x and OVMF edk2-stable202605

Record exact versions and OVMF SHA-256 in the generated release manifest.

## Build and Host Tests

```text
cmake --preset gcc-reference
cmake --build --preset gcc-reference
cmake --preset gcc-host
cmake --build --preset gcc-host
ctest --preset gcc-host
cmake --preset clang-validation
cmake --build --preset clang-validation
cmake --preset clang-host
cmake --build --preset clang-host
ctest --preset clang-host
```

Both profiles must compile project C as freestanding C17 with gated warnings and layout assertions. The GCC build emits the UEFI image, 64 MiB data image, kernel symbols, launch metadata, and manifest. Host tests cover names, hash, CRC, geometry, FAT, directory pairs, paths, and mutation ordering.

## Boot Smoke Test

```text
cmake --build --preset gcc-reference --target run-headless
```

Serial output must show boot milestones, one 512-byte ATA disk, and `InferenceOS>`. Repeat for 20 clean boots as required by SC-001. The harness may report the plan's five-second engineering target separately, but that target is non-gating unless it is added to the specification.

## Mandatory Demonstration

Use `help` for final syntax, then execute this semantic sequence:

```text
devices
diskinfo
format
mount
fsinfo
create TEST.TXT
write TEST.TXT known-content
type TEST.TXT
fileinfo TEST.TXT
hashinfo TEST.TXT
create SECOND.TXT
hashinfo SECOND.TXT
create THIRD.LOG
hashinfo THIRD.LOG
rename TEST.TXT TEST.LOG
hashinfo TEST.LOG
fatinfo TEST.LOG
sync
reboot
mount
type TEST.LOG
hashinfo TEST.LOG
delete SECOND.TXT
sync
shutdown
```

`TEST.TXT` and `SECOND.TXT` share the canonical `TXT` hash; `LOG` normally differs; rename recomputes required metadata; reboot preserves content and companion validity; delete retires both records without harming other chains.

## Automated Resilience

```text
ctest --preset qemu-integration
ctest --preset qemu-persistence
ctest --preset image-corruption
ctest --preset block-faults
```

Coverage must include lifecycle operations; extensions length 0–3; same/different extensions; forced collision; corrupt/inconsistent superblocks; companion CRC/version/algorithm/checksum/hash and association failures; FAT loops/ranges/bad values; fragmentation/full disk; and interrupted create, rename, delete, FAT extension, write, and flush.

## Release Acceptance

Accept only when the full demonstration passes, 20 mutation/sync/reboot cycles preserve acknowledged data, both compiler profiles pass, corruption never causes out-of-volume access or uncontrolled traversal, format docs enable independent parsing, and the manifest records exact tools/firmware/artifact checksums.
