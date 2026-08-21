#!/usr/bin/env python3
"""Generate a deterministic InferenceOS tool, firmware, and artifact manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any, Iterable


SCHEMA_VERSION = 1
VERSION_PATTERN = re.compile(r"(?<!\d)(\d+\.\d+(?:\.\d+)?)(?!\d)")

# Requirements are kept beside probe definitions so a generated manifest shows
# both what ran and whether it matched the reference profile.
DEFAULT_TOOLS: dict[str, tuple[str, tuple[str, ...], str]] = {
    "gcc": ("x86_64-elf-gcc", ("-dumpfullversion", "-dumpversion"), "16.2.0"),
    "binutils-as": ("x86_64-elf-as", ("--version",), "2.45"),
    "binutils-ld": ("x86_64-elf-ld", ("--version",), "2.45"),
    "clang": ("clang", ("--version",), "22.1.8"),
    "lld": ("ld.lld", ("--version",), "22.1.8"),
    "qemu": ("qemu-system-x86_64", ("--version",), "11.1.0"),
    "cmake": ("cmake", ("--version",), "4.1"),
    "ninja": ("ninja", ("--version",), "1.12"),
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_named_path(value: str) -> tuple[str, Path]:
    name, separator, raw_path = value.partition("=")
    if not separator or not name or not raw_path:
        raise argparse.ArgumentTypeError("expected NAME=PATH")
    return name, Path(raw_path)


def parse_tool_override(value: str) -> tuple[str, str]:
    name, separator, executable = value.partition("=")
    if not separator or not name or not executable:
        raise argparse.ArgumentTypeError("expected TOOL_NAME=EXECUTABLE")
    if name not in DEFAULT_TOOLS:
        valid = ", ".join(sorted(DEFAULT_TOOLS))
        raise argparse.ArgumentTypeError(f"unknown tool '{name}'; expected one of: {valid}")
    return name, executable


def normalized_path(path: Path, source_root: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(source_root).as_posix()
    except ValueError:
        return resolved.as_posix()


def first_nonempty_line(text: str) -> str:
    return next((line.strip() for line in text.splitlines() if line.strip()), "")


def version_matches(detected: str | None, required: str) -> bool:
    if detected is None:
        return False
    required_parts = required.split(".")
    detected_parts = detected.split(".")
    return detected_parts[: len(required_parts)] == required_parts


def probe_tool(
    name: str,
    executable: str,
    version_args: Iterable[str],
    required_version: str,
    source_root: Path,
) -> dict[str, Any]:
    resolved_name = shutil.which(executable)
    result: dict[str, Any] = {
        "command": executable,
        "required_version": required_version,
    }
    if resolved_name is None:
        result["status"] = "missing"
        return result

    resolved = Path(resolved_name).resolve()
    result["path"] = normalized_path(resolved, source_root)
    result["sha256"] = sha256_file(resolved)

    try:
        completed = subprocess.run(
            [str(resolved), *version_args],
            check=False,
            capture_output=True,
            text=True,
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        result["status"] = "probe-error"
        result["error"] = str(error)
        return result

    combined = "\n".join(part for part in (completed.stdout, completed.stderr) if part)
    summary = first_nonempty_line(combined)
    match = VERSION_PATTERN.search(summary)
    detected = match.group(1) if match else None
    result["version"] = detected
    result["version_output"] = summary
    result["probe_exit_code"] = completed.returncode
    if completed.returncode != 0 or detected is None:
        result["status"] = "probe-error"
    elif version_matches(detected, required_version):
        result["status"] = "ok"
    else:
        result["status"] = "version-mismatch"
    return result


def describe_file(name: str, path: Path, source_root: Path) -> dict[str, Any]:
    resolved = path.resolve()
    if not resolved.is_file():
        return {
            "name": name,
            "path": normalized_path(resolved, source_root),
            "status": "missing",
        }
    return {
        "name": name,
        "path": normalized_path(resolved, source_root),
        "size": resolved.stat().st_size,
        "sha256": sha256_file(resolved),
        "status": "ok",
    }


def run_git(source_root: Path, *arguments: str) -> str | None:
    try:
        completed = subprocess.run(
            ["git", "-C", str(source_root), *arguments],
            check=False,
            capture_output=True,
            text=True,
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if completed.returncode != 0:
        return None
    return completed.stdout.strip()


def source_identity(source_root: Path, explicit_epoch: int | None) -> dict[str, Any]:
    revision = run_git(source_root, "rev-parse", "HEAD")
    status = run_git(source_root, "status", "--porcelain", "--untracked-files=normal")
    commit_epoch = run_git(source_root, "show", "-s", "--format=%ct", "HEAD")
    environment_epoch = os.environ.get("SOURCE_DATE_EPOCH")

    epoch: int | None = explicit_epoch
    epoch_source = "argument" if explicit_epoch is not None else None
    if epoch is None and environment_epoch:
        try:
            epoch = int(environment_epoch)
            epoch_source = "environment"
        except ValueError as error:
            raise ValueError("SOURCE_DATE_EPOCH must be an integer") from error
    if epoch is None and commit_epoch:
        epoch = int(commit_epoch)
        epoch_source = "git-commit"

    return {
        "revision": revision,
        "dirty": None if status is None else bool(status),
        "source_date_epoch": epoch,
        "source_date_epoch_origin": epoch_source,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/release-manifest.json"),
        help="manifest destination (default: build/release-manifest.json)",
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root used for source identity and relative paths",
    )
    parser.add_argument(
        "--source-date-epoch",
        type=int,
        help="explicit reproducible build epoch; otherwise use environment or Git",
    )
    parser.add_argument(
        "--firmware-version",
        default="edk2-stable202605",
        help="declared OVMF/edk2 release associated with firmware inputs",
    )
    parser.add_argument(
        "--firmware",
        action="append",
        default=[],
        type=parse_named_path,
        metavar="NAME=PATH",
        help="firmware input to hash; may be supplied more than once",
    )
    parser.add_argument(
        "--artifact",
        action="append",
        default=[],
        type=parse_named_path,
        metavar="NAME=PATH",
        help="generated artifact to hash; may be supplied more than once",
    )
    parser.add_argument(
        "--tool",
        action="append",
        default=[],
        type=parse_tool_override,
        metavar="TOOL_NAME=EXECUTABLE",
        help="override a default tool executable; may be supplied more than once",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="return failure when any tool, firmware, or artifact is missing or mismatched",
    )
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    source_root = arguments.source_root.resolve()
    if not source_root.is_dir():
        raise SystemExit(f"source root is not a directory: {source_root}")

    overrides = dict(arguments.tool)
    tools: dict[str, Any] = {}
    for name, (default_executable, version_args, required) in sorted(DEFAULT_TOOLS.items()):
        executable = overrides.get(name, default_executable)
        tools[name] = probe_tool(name, executable, version_args, required, source_root)

    # Python is the executing interpreter, so record it without PATH ambiguity.
    python_required = "3.12"
    python_version = f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}"
    python_path = Path(sys.executable).resolve()
    tools["python"] = {
        "command": sys.executable,
        "path": normalized_path(python_path, source_root),
        "sha256": sha256_file(python_path),
        "required_version": python_required,
        "version": python_version,
        "version_output": first_nonempty_line(sys.version),
        "probe_exit_code": 0,
        "status": "ok" if version_matches(python_version, python_required) else "version-mismatch",
    }

    firmware = {
        name: describe_file(name, path, source_root)
        for name, path in sorted(arguments.firmware)
    }
    artifacts = {
        name: describe_file(name, path, source_root)
        for name, path in sorted(arguments.artifact)
    }

    manifest = {
        "schema_version": SCHEMA_VERSION,
        "source": source_identity(source_root, arguments.source_date_epoch),
        "reference_profile": {
            "architecture": "x86_64",
            "firmware_release": arguments.firmware_version,
            "qemu_machine": "pc-i440fx",
        },
        "tools": tools,
        "firmware": firmware,
        "artifacts": artifacts,
    }

    output = arguments.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    failures = [
        f"tool:{name}"
        for name, item in tools.items()
        if item["status"] != "ok"
    ]
    failures.extend(
        f"firmware:{name}"
        for name, item in firmware.items()
        if item["status"] != "ok"
    )
    failures.extend(
        f"artifact:{name}"
        for name, item in artifacts.items()
        if item["status"] != "ok"
    )

    print(f"wrote {output}")
    if failures:
        print("nonconforming inputs: " + ", ".join(failures), file=sys.stderr)
    return 1 if arguments.strict and failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
