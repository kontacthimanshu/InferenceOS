# Hyper-V Generation 2 Support

InferenceOS now has an experimental Hyper-V platform path alongside the qualified QEMU/q35 path.
It detects Microsoft Hyper-V through CPUID, enables the hypercall page and SynIC, negotiates VMBus,
opens synthetic device channels, and uses StorVSC for the InferenceOS-FS data disk.

The wire definitions and behavior are derived from Microsoft's
[Hyper-V Top-Level Functional Specification](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/tlfs)
and cross-checked against Microsoft's open-source
[OpenVMM](https://github.com/microsoft/openvmm) implementation. Hyper-V support remains experimental
until the physical-host qualification matrix below is completed.

## Build the attachable disks

Run the compiler build in WSL first:

```bash
cmake --preset gcc-debug
cmake --build --preset gcc-debug --target inferenceos-image
```

Then open an elevated Windows PowerShell in the repository and run:

```powershell
& tools/image/build_hyperv_boot_vhdx.ps1 `
  -LoaderPath build/gcc-debug/artifacts/BOOTX64.EFI `
  -KernelPath build/gcc-debug/artifacts/kernel.elf `
  -ModuleDirectory build/gcc-debug/artifacts/system-modules `
  -OutputPath build/gcc-debug/artifacts/inferenceos-hyperv-boot.vhdx `
  -Force

& tools/image/create_hyperv_data_vhdx.ps1 `
  -OutputPath build/gcc-debug/artifacts/inferenceos-hyperv-data.vhdx `
  -Force
```

The equivalent CMake target is `inferenceos-hyperv-images`; it also requires elevated Windows
PowerShell because it uses the Hyper-V and Storage cmdlets.

The boot VHDX is GPT-partitioned and contains a normal FAT32 EFI System Partition. The data VHDX is
a dynamic, unpartitioned, 512-byte-sector disk. Do not initialize, partition, or format the data
VHDX in Windows. InferenceOS formats the whole virtual disk as InferenceOS-FS.

## Create and attach the VM

Run these commands in the same elevated PowerShell session. Assigning `$VmName` first avoids the
`Set-VMFirmware ... VMName is null or empty` error.

```powershell
$VmName = 'InferenceOS-HyperV'
$BootVhdx = (Resolve-Path 'build/gcc-debug/artifacts/inferenceos-hyperv-boot.vhdx').Path
$DataVhdx = (Resolve-Path 'build/gcc-debug/artifacts/inferenceos-hyperv-data.vhdx').Path

New-VM -Name $VmName -Generation 2 -MemoryStartupBytes 1GB -NoVHD
Set-VMMemory -VMName $VmName -DynamicMemoryEnabled $false
Set-VMProcessor -VMName $VmName -Count 1
Set-VM -Name $VmName -AutomaticCheckpointsEnabled $false
Set-VMFirmware -VMName $VmName -EnableSecureBoot Off

Add-VMHardDiskDrive -VMName $VmName -ControllerType SCSI `
  -ControllerNumber 0 -ControllerLocation 0 -Path $BootVhdx
Add-VMHardDiskDrive -VMName $VmName -ControllerType SCSI `
  -ControllerNumber 0 -ControllerLocation 1 -Path $DataVhdx

$BootDrive = Get-VMHardDiskDrive -VMName $VmName |
  Where-Object Path -eq $BootVhdx
Set-VMFirmware -VMName $VmName -FirstBootDevice $BootDrive

& tests/system/hyperv_profile_test.ps1 `
  -VmName $VmName -BootVhdx $BootVhdx -DataVhdx $DataVhdx
Start-VM -Name $VmName
vmconnect.exe localhost $VmName
```

For an existing Generation 2 VM, use only the `Set-*`, `Add-VMHardDiskDrive`, firmware-order, and
profile-test commands. Do not attach `inferenceos-esp.img` or `inferenceos-persistent.raw` directly;
Hyper-V requires the VHDX wrappers.

To deploy a rebuilt kernel while retaining the files already stored on the generated data VHDX,
run the repository update workflow from an elevated Windows PowerShell:

```powershell
Set-Location C:\Users\konta\source\repos\InferenceOS
.\tools\image\recreate_hyperv_vm.ps1 -Force -PreserveDataDisk -Start
```

This rebuilds the current source, replaces the VM and boot VHDX, then reattaches the existing
`inferenceos-hyperv-data.vhdx`. Preservation fails closed if the data VHDX is missing, has an
incompatible size or logical sector size, is not attached exactly once, or the VM has checkpoints.
Omit `-PreserveDataDisk` only when a new blank data disk is intended.

## Disk safety and persistence validation

The loader passes the GPT identity of its EFI partition to the kernel. StorVSC probes SCSI LUNs,
but the platform publishes only a blank whole disk or an existing valid InferenceOS-FS whole disk.
Any disk with GPT/MBR partitions, the boot-partition GUID, malformed partition metadata, foreign
content, or an I/O ambiguity is excluded from formatting and root mounting.

In the CUI, exercise the data disk:

```text
devices
format disk0
mount disk0 /
write /HYPERV.TXT "persistent Hyper-V data"
sync
reboot
type /HYPERV.TXT
```

Repeat after swapping the two VHDXs' SCSI controller locations. `disk0` must still be the data
VHDX. Also boot with only the boot VHDX; no format-capable disk should appear.

## Device behavior

- Keyboard uses the Hyper-V synthetic keyboard protocol and normalizes make/break, modifiers, and
  repeats into the existing input queue.
- Pointer input uses SynthHID 2.0 and handles the standard Hyper-V five-byte absolute mouse report.
  Its 15-bit (`0x7fff`) logical X/Y range is scaled across the complete framebuffer, and malformed
  larger coordinates clamp to the screen edge. This keeps every desktop control, including the
  top-right GUI close button, reachable.
- Display remains the retained UEFI GOP framebuffer. The current qualified handoff is direct
  BGRX8888 at 1024x768. The standalone CUI now draws its prompt, input echo, command output,
  backspace, clear, wrapping, and scrolling directly to that framebuffer while preserving COM1 as
  a diagnostic mirror. Unsupported GOP modes leave the COM1 recovery console available.
- Reboot and shutdown preserve the existing filesystem sync and block flush ordering, then call the
  retained UEFI runtime `ResetSystem` service.

## Qualification status

On 2026-08-28, the documented Generation 2 profile passed 20/20 real-host cold boots to the CUI,
including VMBus, StorVSC, synthetic keyboard, and SynthHID protocol initialization. The real-host
StorVSC durability contract also passed 20/20 fresh boots while preserving the boot VHDX hash.
Interactive keyboard-driven `version`, disk discovery, format, mount, and filesystem diagnostics
passed. GCC/Clang hosted suites, both freestanding images, and the QEMU boot regression remained
green after the host fixes. The subsequent standalone-framebuffer implementation passed its GCC
and Clang renderer/regression suites and emitted `INFERENCEOS:FRAMEBUFFER_CUI_READY` in the retained
QEMU boot evidence. A regenerated boot VHDX still requires a VMConnect visual check on the Hyper-V
host before that specific display path is called host-qualified.

The runtime now installs the real InferenceOS-FS file, directory, VFS lookup/enumeration, and
diagnostic providers. The QEMU guest command audit exercises create/write/append/type/rename/delete,
directory navigation and mutation, record/hash/FAT diagnostics, sync, unmount, and remount against
the on-disk namespace. `diskinfo` also performs read-only filesystem classification and reports the
recognized filesystem plus mount state. Hyper-V file-level persistence, disk attachment permutations, live
GUI/pointer behavior, and the repeated guest-driven power matrix still require retained real-host
evidence. Detailed results and evidence locations are recorded in
`specs/002-hyperv-platform/quickstart.md`.
