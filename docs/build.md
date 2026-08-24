# Building and Running InferenceOS

InferenceOS is an x86-64 UEFI operating-system demonstrator. Its reference build and runtime
environment is Ubuntu 24.04, either directly or under Windows Subsystem for Linux (WSL), with QEMU
q35, OVMF, and TCG. Build commands must be run from the repository root.

## Prerequisites

- A clean repository checkout on Ubuntu 24.04 or Windows with WSL and an Ubuntu distribution.
- Network access, `sudo`, and sufficient space and time to build the pinned toolchain.
- PowerShell 7 (`pwsh`) on a native Linux host. WSL may instead use interoperable Windows
  PowerShell when CMake discovers `powershell.exe`.

The bootstrap installs its Ubuntu package prerequisites and builds these exact versions from the
sources recorded in `tools/bootstrap/versions.json`:

| Component | Required version |
|---|---:|
| GNU binutils | 2.45 |
| GCC cross compiler | 16.2.0 |
| LLVM/Clang/LLD | 22.1.8 |
| QEMU | 11.1.0 |
| OVMF/edk2 | edk2-stable202605 |
| CMake | 4.1.1 |
| Ninja | 1.13.1 |

Host tools are installed outside the repository by default. Override `INFERENCEOS_INSTALL_ROOT`
and `INFERENCEOS_DOWNLOAD_ROOT` when a repository-local or disposable installation is required.
The bootstrap does not modify global shell profiles.

## Bootstrap and verify the toolchain

From Windows PowerShell:

```powershell
./tools/bootstrap/wsl-ubuntu.ps1 -Distribution Ubuntu
./tools/bootstrap/wsl-ubuntu.ps1 -Distribution Ubuntu -Check
```

From Ubuntu:

```bash
./tools/bootstrap/wsl-ubuntu.sh
source "$HOME/.local/share/inferenceos/tools/environment.sh"
./tools/bootstrap/wsl-ubuntu.sh --check
```

When custom bootstrap roots are used, source the `environment.sh` path printed by the bootstrap.
This exports the pinned tool prefix, OVMF code image, and OVMF variable-store template for CMake
and the launcher.

## Clean cross-build matrix

Configure and build each preset in a checkout with no pre-existing `build/` directory:

```bash
cmake --preset gcc-debug
cmake --build --preset gcc-debug
cmake --preset gcc-release
cmake --build --preset gcc-release
cmake --preset clang-debug
cmake --build --preset clang-debug
cmake --preset clang-release
cmake --build --preset clang-release
```

Warnings are errors. GCC and Clang builds use freestanding ISO C17, the source-controlled extension
allowlist, and isolated x86-64 assembly. Do not share a build directory between presets or between
native Windows and WSL: CMake caches contain host-specific absolute paths.

The same clean matrix is enforced by `.github/workflows/build.yml`. CI also runs repository-boundary
validation and native hosted tests with GCC and Clang.

## Package reference images

The current cross-build compiles the production libraries and project-owned UEFI loader. It does
not yet link a final `kernel.elf` or the static Shell/GUI application ELF files. Consequently, image
packaging also requires:

- an existing freestanding x86-64 `kernel.elf`; and
- a version-1 `system_modules.json` whose `source` entries resolve to existing static x86-64 ELF64
  applications and include exactly one required Shell.

Pass their absolute paths while configuring a clean build directory:

```bash
cmake --preset gcc-debug \
  -DINFERENCEOS_KERNEL_ELF_FILE=/absolute/path/to/kernel.elf \
  -DINFERENCEOS_SYSTEM_MODULE_DEFINITION=/absolute/path/to/system_modules.json
```

This explicit prerequisite is a current build-integration limitation, not a claim that external
artifacts are part of a reproducible InferenceOS release. A release-qualified clean-checkout image
must wait until the final kernel and application link targets supply these inputs inside the build.

With those inputs configured, package the primary GCC debug images:

```bash
cmake --build --preset gcc-debug --target inferenceos-image
cmake --build --preset gcc-debug --target inferenceos-test-disk
```

Outputs are created under `build/gcc-debug/artifacts/`:

- the generated `BOOTX64.EFI` and configured `kernel.elf`;
- `inferenceos-esp.img`, a deterministic FAT32 ESP;
- `inferenceos-persistent.raw`, a sparse 64 GiB logical disk;
- versioned packaged system modules and `modules.manifest`;
- `*.manifest.json` image sidecars containing canonical recipes and content identities.

The persistent disk has a large logical size but consumes little physical storage when the host
filesystem supports sparse files. Copying it with a tool that expands sparse ranges may consume the
full 64 GiB.

## Launch

The simplest marker-checked boot is:

```bash
cmake --build --preset gcc-debug --target test-boot
```

For an interactive display, invoke the launcher directly:

```powershell
./tools/test/run_inferenceos.ps1 `
  -EspPath build/gcc-debug/artifacts/inferenceos-esp.img `
  -PersistentDiskPath build/gcc-debug/artifacts/inferenceos-persistent.raw `
  -Headless:$false
```

The launcher requires the environment's `INFERENCEOS_OVMF_CODE` and `INFERENCEOS_OVMF_VARS`, or
equivalent explicit parameters. Its portable reference profile is fixed to QEMU 11.1.0,
`q35,accel=tcg`, `qemu64`, one virtual CPU, separate OVMF code/variable drives, standard VGA, a raw
ESP, and a raw virtio-blk persistent disk.

## Validation

Run fast repository checks from PowerShell:

```powershell
./tools/test/validate_source_layout.ps1
./tools/test/validate_dependencies.ps1
./tests/system/bootstrap_test.ps1
./tests/system/artifact_manifest_test.ps1
./tests/system/qemu_profile_test.ps1
```

Hosted C tests require a native, non-cross-compiling configuration. For GCC:

```bash
cmake -S . -B build/host-gcc -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_ASM_COMPILER=gcc \
  -DINFERENCEOS_BUILD_HOST_TESTS=ON \
  -DINFERENCEOS_WARNINGS_AS_ERRORS=ON
cmake --build build/host-gcc
ctest --test-dir build/host-gcc --output-on-failure --no-tests=error
```

Replace both compiler values with `clang` and use `build/host-clang` for the Clang run.

The complete primary-toolchain QEMU story matrix can be launched after building both images:

```powershell
./tools/test/run_qemu_tests.ps1 `
  -EspPath build/gcc-debug/artifacts/inferenceos-esp.img `
  -PersistentDiskPath build/gcc-debug/artifacts/inferenceos-persistent.raw `
  -ReleaseMatrix -RetainSuccessfulArtifacts
```

The matrix covers boot/GUI/recovery, format/mount, File Explorer, reboot persistence, and directory
interoperability. It writes `evidence-manifest.json` with suite outcomes and hashed evidence beneath
`build/qemu-tests/`. A dry-run (`-DryRun`) validates and records the matrix without starting QEMU.

## Troubleshooting

- A version mismatch is an error by design. Re-run the bootstrap and its `--check` mode; do not
  silently substitute a host tool.
- If CMake reports Windows and `/mnt/c/...` cache paths disagree, use the host that originally
  configured that build directory or configure a new directory.
- If OVMF paths are missing, source the generated environment file before configuring and running.
- Failed QEMU tests retain their command, serial transcript, stdout/stderr, and result manifests in
  the reported artifact directory.
- See [limitations.md](limitations.md) before interpreting behavior as a production OS guarantee.

## Licensing

InferenceOS project-authored source is available under the MIT License; see `LICENSE`. Bootstrap
dependencies remain under their respective upstream licenses and are downloaded from the URLs in
`tools/bootstrap/versions.json`. The project license does not relicense those external tools or
firmware.
