"""QEMU acceptance tests for the first boot-to-prompt user story.

The suite is intentionally driven entirely through COM1 and QMP.  Configure it
with absolute paths in these environment variables:

* ``INFERENCEOS_QEMU`` - qemu-system-x86_64 executable
* ``INFERENCEOS_OVMF_CODE`` - read-only OVMF code image
* ``INFERENCEOS_BOOT_IMAGE`` - bootable InferenceOS disk image
* ``INFERENCEOS_DATA_IMAGE`` - optional persistent data disk
* ``INFERENCEOS_OVMF_VARS`` - optional writable OVMF variables template
* ``INFERENCEOS_PANIC_BOOT_IMAGE`` - optional image that panics during boot

Missing normal-boot inputs skip the VM tests rather than turning a developer's
host configuration into a product failure.  The configured QEMU CI preset is
responsible for supplying them.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
import time
import unittest
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))
from qemu_harness import QemuHarness  # noqa: E402


PROMPT = b"InferenceOS>"
COMMAND_TIMEOUT_SECONDS = 5.0
REQUIRED_CORE_HELP_COMMANDS = (b"help", b"version", b"clear")


def _required_path(variable: str) -> Path:
    value = os.environ.get(variable)
    if not value:
        raise unittest.SkipTest(f"{variable} is not configured")
    path = Path(value).resolve()
    if not path.is_file():
        raise unittest.SkipTest(f"{variable} does not name a file: {path}")
    return path


def _optional_path(variable: str) -> Path | None:
    value = os.environ.get(variable)
    if not value:
        return None
    path = Path(value).resolve()
    if not path.is_file():
        raise unittest.SkipTest(f"{variable} does not name a file: {path}")
    return path


def _qemu_path() -> Path:
    value = os.environ.get("INFERENCEOS_QEMU")
    if not value:
        raise unittest.SkipTest("INFERENCEOS_QEMU is not configured")
    resolved = shutil.which(value)
    path = Path(resolved if resolved else value).resolve()
    if not path.is_file():
        raise unittest.SkipTest(f"INFERENCEOS_QEMU does not name an executable: {path}")
    return path


class BootPromptTranscriptTests(unittest.TestCase):
    """Black-box serial transcript tests using actual emulated PS/2 input."""

    def setUp(self) -> None:
        self.qemu = _qemu_path()
        self.ovmf_code = _required_path("INFERENCEOS_OVMF_CODE")
        self.source_boot = _required_path("INFERENCEOS_BOOT_IMAGE")
        self.source_data = _optional_path("INFERENCEOS_DATA_IMAGE")
        self.source_vars = _optional_path("INFERENCEOS_OVMF_VARS")
        self.temporary = tempfile.TemporaryDirectory(prefix="inferenceos-boot-prompt-")
        self.addCleanup(self.temporary.cleanup)

    def _copy(self, source: Path | None, name: str) -> Path | None:
        if source is None:
            return None
        destination = Path(self.temporary.name) / name
        shutil.copy2(source, destination)
        return destination

    def _harness(self, boot_image: Path | None = None) -> QemuHarness:
        # Every scenario gets clean mutable disks and variable storage.
        boot = self._copy(boot_image or self.source_boot, "boot.img")
        assert boot is not None
        return QemuHarness(
            qemu=self.qemu,
            firmware_code=self.ovmf_code,
            firmware_vars=self._copy(self.source_vars, "OVMF_VARS.fd"),
            boot_image=boot,
            data_image=self._copy(self.source_data, "data.img"),
            transcript_path=Path(self.temporary.name) / "serial.log",
            timeout=float(os.environ.get("INFERENCEOS_QEMU_TIMEOUT", "30")),
        )

    def _wait_for_new_prompt(
        self, harness: QemuHarness, previous_count: int, timeout: float = COMMAND_TIMEOUT_SECONDS
    ) -> bytes:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            transcript = harness.transcript
            if transcript.count(PROMPT) > previous_count:
                return transcript
            if harness.process is not None and harness.process.poll() is not None:
                self.fail(f"QEMU exited before returning to the prompt: {transcript[-1000:]!r}")
            time.sleep(0.02)
        self.fail(f"prompt did not recur; transcript tail={harness.transcript[-1000:]!r}")

    def _run_command(
        self, harness: QemuHarness, send: Callable[[QemuHarness], None]
    ) -> bytes:
        before = harness.transcript
        prompt_count = before.count(PROMPT)
        send(harness)
        after = self._wait_for_new_prompt(harness, prompt_count)
        return after[len(before):]

    def test_boot_milestones_reach_banner_then_ready_prompt(self) -> None:
        with self._harness() as harness:
            transcript = harness.wait_for(PROMPT)
            before_prompt = transcript[: transcript.index(PROMPT)]
            self.assertIn(b"InferenceOS", before_prompt, "banner must precede the ready prompt")
            self.assertNotIn(b"PANIC:", transcript)
            self.assertFalse(harness.transcript_truncated)

    def test_printable_input_backspace_enter_and_help(self) -> None:
        with self._harness() as harness:
            harness.wait_for(PROMPT)

            def edited_help(target: QemuHarness) -> None:
                target.send_text("helx")
                target.press_backspace()
                target.send_text("p")
                target.send_key("ret")

            response = self._run_command(harness, edited_help).lower()
            self.assertNotIn(b"unknown command", response)
            for command in REQUIRED_CORE_HELP_COMMANDS:
                self.assertIn(command, response)

    def test_unknown_printable_command_reports_error_and_recovers(self) -> None:
        with self._harness() as harness:
            harness.wait_for(PROMPT)
            response = self._run_command(
                harness, lambda target: target.send_line("no-such_command!123")
            ).lower()
            self.assertIn(b"unknown command", response)
            self.assertTrue(response.rstrip().endswith(PROMPT.lower()))

    def test_panic_transcript_is_bounded_and_does_not_reach_prompt(self) -> None:
        panic_image = _optional_path("INFERENCEOS_PANIC_BOOT_IMAGE")
        if panic_image is None:
            self.skipTest("INFERENCEOS_PANIC_BOOT_IMAGE is not configured")
        with self._harness(panic_image) as harness:
            transcript = harness.wait_for(b"PANIC:")
            deadline = time.monotonic() + 0.25
            while b"\n" not in transcript[transcript.index(b"PANIC:"):] and time.monotonic() < deadline:
                time.sleep(0.01)
                transcript = harness.transcript
            panic_tail = transcript[transcript.index(b"PANIC:"):]
            self.assertIn(b"\n", panic_tail, "panic diagnostic must terminate its serial line")
            panic_line = panic_tail.splitlines()[0]
            self.assertLessEqual(len(panic_line), len(b"PANIC: ") + 256)
            self.assertNotIn(PROMPT, panic_tail)


if __name__ == "__main__":
    unittest.main()
