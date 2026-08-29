# InferenceOS

InferenceOS is an experimental, open-source x86-64 operating-system and filesystem demonstrator.
It boots through UEFI in a pinned QEMU q35/OVMF environment and provides a standalone CUI, a GUI
desktop, persistent storage, one shared VFS namespace, and the versioned InferenceOS-FS filesystem.
An experimental Hyper-V Generation 2 backend is available for VMBus/StorVSC and synthetic input;
see [docs/hyperv.md](docs/hyperv.md) for its build and qualification workflow.

The defining experiment keeps an authoritative extension in each internal primary directory record
and stores a separate extension-hash companion record. Ordinary CUI, GUI, and application views hide
both values; privileged diagnostics and raw-disk analysis can reveal them. This interface rule is not
encryption, access control, or a security guarantee.

## Demonstrated scope

- CUI and GUI views over the same VFS namespace, including File Explorer and filtered type viewers;
  the command prompt remains in the standalone CUI rather than opening automatically in GUI mode.
- A distinct InferenceOS-FS volume with a sparse 64 GiB reference disk, durable save ordering,
  metadata validation, and reboot-persistence workflows.
- Shell-mediated, opaque application file services that do not expose raw extensions or hashes.
- Reproducible freestanding C17 builds with pinned GCC and Clang profiles.
- An optional Extension Registry research path that always falls back to authoritative directory
  metadata.

The Extension Registry is disabled by default. Benchmark evidence may make an implementation
proposal-eligible, but this README claims no performance improvement or default enablement.

InferenceOS is not production-ready, hardened, multi-user, network-complete, POSIX-compatible, or a
general-purpose replacement for an established operating system. InferenceOS-FS is not FAT32-compatible.

## Build and validation

Start with [docs/build.md](docs/build.md) and the
[feature quickstart](specs/001-inferenceos/quickstart.md). The exact supported boundary and deferred
capabilities are documented in [docs/limitations.md](docs/limitations.md). The final constitutional
and release-claim disposition is recorded in
[docs/validation/constitution-check.md](docs/validation/constitution-check.md).

InferenceOS is licensed under the [MIT License](LICENSE).
