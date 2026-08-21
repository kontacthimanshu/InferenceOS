#!/usr/bin/env python3
"""Launch the documented one-vCPU InferenceOS QEMU/OVMF profile."""

from __future__ import annotations

import argparse
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path


def _file(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise ValueError(f"{label} does not name a file: {resolved}")
    return resolved


def build_command(args: argparse.Namespace, variables: Path | None) -> list[str]:
    command = [
        str(args.qemu), "-machine", "pc,accel=tcg", "-cpu", "qemu64",
        "-smp", "1", "-m", f"{args.memory}M", "-display", "none",
        "-monitor", "none", "-drive",
        f"if=pflash,format=raw,readonly=on,file={args.firmware_code}",
    ]
    if variables is not None:
        command.extend(["-drive", f"if=pflash,format=raw,file={variables}"])
    command.extend([
        "-drive", f"if=ide,index=0,media=disk,format=raw,file={args.boot_image}",
        "-serial", "stdio",
    ])
    if args.data_image is not None:
        command.extend([
            "-drive", f"if=ide,index=1,media=disk,format=raw,file={args.data_image}"
        ])
    command.extend(args.qemu_arg)
    return command


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", required=True, type=Path)
    parser.add_argument("--firmware-code", required=True, type=Path)
    parser.add_argument("--firmware-vars", type=Path)
    parser.add_argument("--boot-image", required=True, type=Path)
    parser.add_argument("--data-image", type=Path)
    parser.add_argument("--memory", type=int, default=128)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--qemu-arg", action="append", default=[])
    args = parser.parse_args()
    try:
        qemu = shutil.which(str(args.qemu))
        args.qemu = Path(qemu).resolve() if qemu else _file(args.qemu, "QEMU executable")
        args.firmware_code = _file(args.firmware_code, "OVMF code")
        args.boot_image = _file(args.boot_image, "boot image")
        args.data_image = _file(args.data_image, "data image") if args.data_image else None
        args.firmware_vars = _file(args.firmware_vars, "OVMF variables") if args.firmware_vars else None
        if args.memory < 64 or args.memory > 4096:
            raise ValueError("--memory must be between 64 and 4096 MiB")
    except ValueError as error:
        parser.error(str(error))

    with tempfile.TemporaryDirectory(prefix="inferenceos-qemu-") as temporary:
        variables_copy = None
        if args.firmware_vars is not None:
            variables_copy = Path(temporary) / "OVMF_VARS.fd"
            shutil.copyfile(args.firmware_vars, variables_copy)
        command = build_command(args, variables_copy)
        print(shlex.join(command), flush=True)
        if args.dry_run:
            return 0
        return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
