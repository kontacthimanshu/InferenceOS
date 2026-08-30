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
cmake --build --preset gcc-debug --target inferenceos-image inferenceos-test-disk
cmake --preset gcc-release
cmake --build --preset gcc-release --target inferenceos-image inferenceos-test-disk
cmake --preset clang-debug
cmake --build --preset clang-debug --target inferenceos-image inferenceos-test-disk
cmake --preset clang-release
cmake --build --preset clang-release --target inferenceos-image inferenceos-test-disk
```

Warnings are errors. GCC and Clang builds use freestanding ISO C17, the source-controlled extension
allowlist, and isolated x86-64 assembly. Do not share a build directory between presets or between
native Windows and WSL: CMake caches contain host-specific absolute paths.

The same clean matrix is enforced by `.github/workflows/build.yml`. CI also runs repository-boundary
validation and native hosted tests with GCC and Clang.

## Package reference images

Package the primary GCC debug images directly from a clean checkout:

```bash
cmake --build --preset gcc-debug --target inferenceos-image
cmake --build --preset gcc-debug --target inferenceos-test-disk
```

Outputs are created under `build/gcc-debug/artifacts/`:

- the project-built `BOOTX64.EFI`, `kernel.elf`, and eight static application ELFs;
- `inferenceos-esp.img`, a deterministic FAT32 ESP;
- `inferenceos-persistent.raw`, a sparse 64 GiB logical disk;
- target-generated version-1 `system_modules.json`, packaged modules, `modules.manifest`, and
  `modules.sha256`;
- the generated, kernel-embedded `fonts/inferenceos-console-12x24.alpha4` asset and its packaged copy;
- kernel/application `*.map` and sorted `*.sym` debugging artifacts, plus `BOOTX64.map`;
- `*.manifest.json` image sidecars containing canonical recipes and content identities.

No `kernel.elf`, application ELF, module definition, or font is accepted as an external packaging
input. CMake generates the module definition from the named kernel/application targets, expands the
checked-in alpha4 font source, checks every packaged hash, and makes `inferenceos-image` depend on
the complete target graph. The font atlas is derived from JetBrains Mono Regular and remains under
the SIL Open Font License 1.1; see `assets/fonts/LICENSE.txt` and `assets/fonts/OFL.txt`.

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
./tools/test/validate_dependencies.ps1 -SelfTest `
  -EvidencePath build/validation/dependency-boundaries.json
./tools/test/generate_validation_report.ps1 `
  -EvidenceDirectory build/validation -OutputDirectory build/validation
./tests/system/bootstrap_test.ps1
./tests/system/artifact_manifest_test.ps1
./tests/system/qemu_profile_test.ps1
```

The dependency validator scans every production C/header file and enforces four static boundaries:

- GUI, Shell, and applications cannot include or call VFS, InferenceOS-FS, block, or virtio-blk APIs.
- InferenceOS-FS cannot depend on GUI/CUI/Shell APIs or the concrete virtio-blk driver.
- VFS cannot depend on InferenceOS-FS internals or a concrete storage driver.
- Code outside the generic block layer and virtio-blk driver cannot communicate with virtio-blk.

`-SelfTest` exercises both include and symbol detection for every rule. `-EvidencePath` writes a
deterministic, repository-relative JSON report containing the rule catalog, inspected-file count,
self-test result, and any violations. The `inferenceos-validate-dependencies` target and the
`dependency-validation` CTest use `build/<configuration>/validation/dependency-boundaries.json`.

The validation-report generator reads SC-001 through SC-020 directly from the feature specification
and maps each criterion to its implementation tasks, validation tasks, source tests, CTest names,
QEMU suites, artifact contracts, and reproduction commands. It discovers retained dual-compiler
JUnit XML, QEMU release manifests, dependency evidence, registry benchmark reports, and the
clean-checkout report, then emits deterministic `validation-traceability.json` and
`validation-traceability.md`. Missing evidence is reported as `not-collected` or `incomplete`;
use `-RequireComplete` only after the complete release matrix has been archived. The equivalent
CMake entry point is `inferenceos-validation-report`.

Hosted C tests require a native, non-cross-compiling configuration. The checked-in presets keep
these builds separate from the cross-compiled OS images:

```bash
cmake --preset gcc-host-debug
cmake --build --preset gcc-host-debug
ctest --preset gcc-host

cmake --preset clang-host-debug
cmake --build --preset clang-host-debug
ctest --preset clang-host
```

Use `gcc-integration`, `gcc-fault`, or `gcc-contract` (and the equivalent `clang-*` preset) to run
the other hosted test groups from the same native build trees.

The complete primary-toolchain QEMU story matrix can be launched after building both images:

```powershell
./tools/test/run_qemu_tests.ps1 `
  -EspPath build/gcc-debug/artifacts/inferenceos-esp.img `
  -PersistentDiskPath build/gcc-debug/artifacts/inferenceos-persistent.raw `
  -ReleaseMatrix -RetainSuccessfulArtifacts
```

The matrix covers boot/GUI/recovery, format/mount, File Explorer, reboot persistence, directory
interoperability, and display-safe CUI file commands. It writes `evidence-manifest.json` with suite
outcomes and hashed evidence beneath `build/qemu-tests/`. A dry-run (`-DryRun`) validates and
records the matrix without starting QEMU.

Run the matched registry research workload and generate its report directly from the packaged
images with:

```bash
cmake --build --preset gcc-debug --target benchmark-registry-qemu
```

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
firmware. The JetBrains Mono-derived console atlas is licensed under the SIL Open Font License 1.1,
with provenance in `assets/fonts/LICENSE.txt` and the full terms in `assets/fonts/OFL.txt`.

## Hyper-V Generation 2 images

After `inferenceos-image` succeeds, build the GPT/ESP boot VHDX and blank data VHDX. The scripts
must run from an elevated Windows PowerShell because they use the Hyper-V and Storage cmdlets; use
the commands in [hyperv.md](hyperv.md) when the compiler build directory was configured through WSL.
For a build tree configured natively on Windows, the equivalent target is:

```powershell
cmake --build --preset gcc-debug --target inferenceos-hyperv-images
```

Outputs:

- `build/gcc-debug/artifacts/inferenceos-hyperv-boot.vhdx` — GPT with a FAT32 EFI System Partition;
- `build/gcc-debug/artifacts/inferenceos-hyperv-data.vhdx` — dynamic 64 GiB, unpartitioned, 512-byte
  logical sectors, intended for whole-disk InferenceOS-FS formatting inside the guest.

See [hyperv.md](hyperv.md) for VM creation, attachment order, boot-disk safety, and persistence tests.
