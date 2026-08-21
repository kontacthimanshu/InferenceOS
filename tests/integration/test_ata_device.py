"""QEMU contract tests for the polling ATA PIO block device.

The kernel test build selected by ``INFERENCEOS_ATA_TEST_BOOT_IMAGE`` runs a
bounded ATA self-test before entering the prompt.  It emits one structured
serial result per case so failures that cannot safely be induced through the
production shell (notably a stuck-BSY timeout) remain observable without
adding test commands to the release kernel.

Required environment variables:

* ``INFERENCEOS_QEMU``
* ``INFERENCEOS_OVMF_CODE``
* ``INFERENCEOS_ATA_TEST_BOOT_IMAGE``
* ``INFERENCEOS_DATA_IMAGE`` (a disposable raw IDE disk)

``INFERENCEOS_OVMF_VARS`` and ``INFERENCEOS_QEMU_TIMEOUT`` are optional.
"""

from __future__ import annotations

import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from qemu_harness import QemuHarness  # noqa: E402


PROMPT = b"InferenceOS>"
RESULT_PREFIX = b"ATA-TEST "


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


class AtaDeviceIntegrationTests(unittest.TestCase):
    """Exercise the real PIIX IDE path through the generic block interface."""

    def setUp(self) -> None:
        self.qemu = _qemu_path()
        self.ovmf_code = _required_path("INFERENCEOS_OVMF_CODE")
        self.source_boot = _required_path("INFERENCEOS_ATA_TEST_BOOT_IMAGE")
        self.source_data = _required_path("INFERENCEOS_DATA_IMAGE")
        self.source_vars = _optional_path("INFERENCEOS_OVMF_VARS")
        self.temporary = tempfile.TemporaryDirectory(prefix="inferenceos-ata-test-")
        self.addCleanup(self.temporary.cleanup)

    def _copy(self, source: Path | None, name: str) -> Path | None:
        if source is None:
            return None
        destination = Path(self.temporary.name) / name
        shutil.copy2(source, destination)
        return destination

    def _harness(self) -> QemuHarness:
        boot = self._copy(self.source_boot, "boot.img")
        data = self._copy(self.source_data, "data.img")
        assert boot is not None and data is not None
        return QemuHarness(
            qemu=self.qemu,
            firmware_code=self.ovmf_code,
            firmware_vars=self._copy(self.source_vars, "OVMF_VARS.fd"),
            boot_image=boot,
            data_image=data,
            transcript_path=Path(self.temporary.name) / "serial.log",
            timeout=float(os.environ.get("INFERENCEOS_QEMU_TIMEOUT", "30")),
        )

    def _result_line(self, case: str) -> bytes:
        marker = RESULT_PREFIX + case.encode("ascii") + b" "
        with self._harness() as harness:
            transcript = harness.wait_for(marker + b"PASS")
            harness.wait_for(PROMPT)
            self.assertNotIn(b"PANIC:", transcript)
            self.assertFalse(harness.transcript_truncated)
            for line in harness.transcript.splitlines():
                if line.startswith(marker):
                    self.assertRegex(line, rb"^ATA-TEST [a-z-]+ PASS(?: .+)?$")
                    return line
        self.fail(f"missing ATA result for {case}")

    def test_identify_reports_lba28_geometry(self) -> None:
        line = self._result_line("identify")
        sector_size = re.search(rb"\bsector-size=(\d+)\b", line)
        sectors = re.search(rb"\bsectors=(\d+)\b", line)
        self.assertIsNotNone(sector_size)
        self.assertIsNotNone(sectors)
        self.assertEqual(512, int(sector_size.group(1)))
        self.assertGreater(int(sectors.group(1)), 0)
        self.assertIn(b"lba28=1", line)

    def test_sector_read_write_round_trip(self) -> None:
        line = self._result_line("read-write")
        self.assertIn(b"sectors-completed=2", line)
        self.assertIn(b"pattern-match=1", line)

    def test_status_is_ready_after_identification(self) -> None:
        line = self._result_line("status")
        self.assertIn(b"status=ready", line)
        self.assertIn(b"error=none", line)

    def test_stuck_busy_poll_returns_bounded_timeout(self) -> None:
        line = self._result_line("timeout")
        self.assertIn(b"error=timeout", line)
        self.assertIn(b"sectors-completed=0", line)
        polls = re.search(rb"\bpolls=(\d+)\b", line)
        limit = re.search(rb"\blimit=(\d+)\b", line)
        self.assertIsNotNone(polls)
        self.assertIsNotNone(limit)
        self.assertGreater(int(polls.group(1)), 0)
        self.assertLessEqual(int(polls.group(1)), int(limit.group(1)))

    def test_out_of_range_request_is_rejected_before_io(self) -> None:
        line = self._result_line("bounds")
        self.assertIn(b"error=out-of-range", line)
        self.assertIn(b"sectors-completed=0", line)
        self.assertIn(b"port-io=0", line)

    def test_cache_flush_completes_after_prior_write(self) -> None:
        line = self._result_line("flush")
        self.assertIn(b"error=none", line)
        self.assertIn(b"writes-before-flush=1", line)
        self.assertIn(b"durable=1", line)


if __name__ == "__main__":
    unittest.main()
