# Quickstart: Validate Hyper-V Platform Support

## Prerequisites

- Complete the pinned WSL/GCC toolchain bootstrap from `docs/build.md`.
- Enable Hyper-V and run PowerShell as Administrator for image and VM operations.
- Use a Generation 2 VM with one processor, 512 MiB static memory, Secure Boot disabled, automatic
  checkpoints disabled, and no dynamic memory.

## Build

```bash
cmake --preset gcc-debug
cmake --build --preset gcc-debug --target inferenceos-image
```

Create the VHDXs afterward from elevated Windows PowerShell with the two scripts documented in
`docs/hyperv.md`. The aggregate `inferenceos-hyperv-images` target is also available when the build
tree is native Windows or WSL was launched from an elevated Windows terminal.

Expected outputs:

```text
build/gcc-debug/artifacts/inferenceos-hyperv-boot.vhdx
build/gcc-debug/artifacts/inferenceos-hyperv-data.vhdx
build/gcc-debug/artifacts/*.manifest.json
```

The boot VHDX contains a GPT FAT32 ESP. The data VHDX is dynamic, at least 64 GiB, 512-byte logical
sector, unpartitioned, and blank. Windows must not initialize or format it.

After source changes, an existing test VM can be refreshed without discarding its InferenceOS-FS
files by running `tools/image/recreate_hyperv_vm.ps1 -Force -PreserveDataDisk -Start` from elevated
Windows PowerShell. The preservation path rejects VMs with checkpoints and validates the retained
data VHDX before replacing the VM and boot disk.

## Configure the VM

Attach both VHDXs to the Generation 2 SCSI controller. Put the boot VHDX first in firmware boot
order; do not rely on controller location for guest safety. Configure COM1 to a named pipe.

## Validate CUI and Disk Safety

Boot and run:

```text
version
devices
diskinfo disk0
format disk0
mount disk0 /
fsinfo
write /HYPERV.TXT "persistent Hyper-V data"
sync
reboot
type /HYPERV.TXT
```

Expected: only the blank data VHDX is `disk0`; the boot VHDX is protected and cannot be formatted;
before formatting, `diskinfo disk0` reports `filesystem=none filesystem_state=blank`; afterward it
reports `filesystem=InferenceOS-FS` and the current mount state. The file survives reboot. Repeat
with boot/data controller locations reversed and with only the boot disk attached. Hash the boot
disk before and after safety tests and require no change.

## Validate GUI and Power

Run `gui`, exercise keyboard, pointer, terminal, and File Explorer over the same file, then test
`shutdown`. If GOP is unsupported, require an explicit GUI-unavailable diagnostic and a responsive
CUI instead of a boot failure.

## Regression Matrix

Run hosted GCC/Clang tests, dependency validation, both cross builds, and the complete existing QEMU
release matrix. Hyper-V remains experimental until 20 cold boots, 20 persistence cycles, disk
permutations, storage faults, GUI/fallback, reboot, and shutdown all retain evidence.

## Implementation and Host Validation (2026-08-28)

- Complete GCC and Clang hosted suites: 65/65 passed with each compiler, including the Hyper-V
  protocol, ring, StorVSC codec, input, disk-safety, dependency, and existing virtio regressions.
- Freestanding GCC and Clang debug images: `inferenceos-image` built successfully with both
  supported compilers.
- Existing QEMU boot regression: `test-boot` reached `INFERENCEOS:CUI_READY`.
- The standalone CUI now renders through the retained GOP surface and mirrors output to COM1. Its
  shared text renderer passed the complete 65-test GCC and Clang hosted suites, both freestanding
  image builds, and a retained QEMU boot that reached `INFERENCEOS:FRAMEBUFFER_CUI_READY` followed
  by `INFERENCEOS:CUI_READY`. Evidence:
  `build/gcc-debug/qemu-tests/framebuffer-cui-20260828T002042311Z-23008/serial.log` and the
  pixel-level GOP capture `build/gcc-debug/qemu-framebuffer-capture/cui.png`, which visibly contains
  the `InferenceOS>` prompt.
- Existing QEMU UEFI reboot/persistence regression: 20/20 cycles passed.
- PowerShell parser validation passed for both VHDX builders and both Hyper-V validation scripts.
- Real Hyper-V host profile passed for `InferenceOS-HyperV`: Generation 2, one processor, 1 GiB
  static memory, automatic checkpoints disabled, Secure Boot disabled, boot VHDX first in firmware
  order, and the data VHDX attached as the second SCSI disk.
- Real Hyper-V cold-boot qualification passed 20/20. Every cycle reached `INFERENCEOS:CUI_READY`
  with synthetic keyboard and mouse protocol readiness and no panic. Guest marker latency was
  256-288 ms (265 ms average). Evidence: `build/gcc-debug/hyperv-diagnostics/cold-boot-matrix.json`.
- Interactive COM1/WMI qualification ran `version`, `devices`, `diskinfo disk0`, `format disk0`,
  `mount disk0 /`, and `fsinfo`. Only the 64 GiB data VHDX appeared as `disk0`; it formatted and
  mounted read/write as `INFOSFS1`. The Hyper-V keyboard path accepted spaces, punctuation,
  uppercase input, quotes, and Enter through explicit Set-1 scan codes.
- The rebuilt QEMU guest command audit verified that `diskinfo disk0` reports
  `filesystem=InferenceOS-FS filesystem_state=mounted mount_state=read_write` while retaining the
  complete CUI filesystem workflow. Evidence:
  `build/gcc-debug/qemu-tests/cui-command-audit/diskinfo-filesystem-audit-20260828T062905598Z-12200/serial.log`.
- The real-host StorVSC durability contract passed 20/20 fresh boots. Each cycle read the preceding
  durable digest, wrote and flushed the next value, and returned the ordered persistence markers.
  The boot VHDX SHA-256 remained
  `41C08B6019463343D67F024B9F76E8B1224F6E48AEF4E07FF539DF7263A95F41` before and after the matrix.
  Evidence: `build/gcc-debug/hyperv-diagnostics/persistence-matrix.json` and the per-cycle COM1 logs.

Generation 2 boot, CUI input, VMBus/StorVSC discovery, cold-boot stability, and the low-level durable
storage contract are therefore host-qualified. The runtime now installs path-aware file and
directory providers plus real record/hash/FAT diagnostic snapshots. A retained QEMU guest audit
completed the create/write/append/type/dir/rename/diagnostic/sync/delete/rmdir/unmount/remount
workflow with `INFERENCEOS:CUI_COMMAND_AUDIT_PASS`. Full release qualification remains pending:
the same file-level workflow still needs retained Hyper-V host evidence, as do attachment-order and
boot-only permutations, live pointer/GUI operation, and repeated guest-driven reboot/shutdown.
