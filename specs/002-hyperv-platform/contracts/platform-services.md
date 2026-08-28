# Contract: Platform Services

The kernel selects exactly one immutable platform instance after memory, interrupts, and scheduler
foundation initialization and before hardware input or storage probing.

Required operations:

- detect and report platform identity;
- initialize input, where keyboard is required and pointer may degrade;
- poll/service platform messages and normalized input;
- initialize the primary eligible persistent block device;
- report the most precise bounded initialization stage;
- perform the final reboot or shutdown transition after the power controller has synchronized and
  flushed storage.

QEMU/q35 binds PS/2, virtio-blk PCI, and the current q35 power ports. Hyper-V binds VMBus devices and
UEFI runtime reset. Unsupported environments fail explicitly. No VFS, filesystem, Shell, GUI, or
application interface receives a platform-specific type.

