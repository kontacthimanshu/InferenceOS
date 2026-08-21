"""Bounded headless QEMU/OVMF integration-test harness.

The harness uses TCP sockets for both COM1 and QMP so it behaves the same on
Windows and POSIX hosts.  It deliberately has no third-party dependencies.
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import threading
import time
from pathlib import Path
from typing import Any, Iterable, Sequence


class QemuHarnessError(RuntimeError):
    """Base error raised by the integration harness."""


class QemuTimeoutError(QemuHarnessError):
    """Raised when a bounded harness operation times out."""


class QemuExitedError(QemuHarnessError):
    """Raised when QEMU exits before the requested observation."""


class QmpError(QemuHarnessError):
    """Raised when QMP rejects a command or returns invalid data."""


_BASE_KEYS = {
    " ": "spc", "`": "grave_accent", "1": "1", "2": "2", "3": "3",
    "4": "4", "5": "5", "6": "6", "7": "7", "8": "8", "9": "9",
    "0": "0", "-": "minus", "=": "equal", "[": "bracket_left",
    "]": "bracket_right", "\\": "backslash", ";": "semicolon",
    "'": "apostrophe", ",": "comma", ".": "dot", "/": "slash",
}
_SHIFTED_KEYS = dict(zip("~!@#$%^&*()_+{}|:\"<>?", "`1234567890-=[]\\;',./"))


def _free_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


class QemuHarness:
    """Own a headless QEMU process, its serial transcript, and its QMP link."""

    def __init__(
        self,
        *,
        qemu: str | Path,
        firmware_code: str | Path,
        boot_image: str | Path,
        data_image: str | Path | None = None,
        firmware_vars: str | Path | None = None,
        timeout: float = 30.0,
        transcript_path: str | Path | None = None,
        max_transcript_bytes: int = 1024 * 1024,
        extra_args: Sequence[str] = (),
    ) -> None:
        if timeout <= 0 or max_transcript_bytes <= 0:
            raise ValueError("timeout and max_transcript_bytes must be positive")
        self.qemu = Path(qemu)
        self.firmware_code = Path(firmware_code)
        self.firmware_vars = Path(firmware_vars) if firmware_vars else None
        self.boot_image = Path(boot_image)
        self.data_image = Path(data_image) if data_image else None
        self.timeout = timeout
        self.transcript_path = Path(transcript_path) if transcript_path else None
        self.max_transcript_bytes = max_transcript_bytes
        self.extra_args = list(extra_args)

        self.process: subprocess.Popen[bytes] | None = None
        self._serial: socket.socket | None = None
        self._qmp: socket.socket | None = None
        self._qmp_buffer = bytearray()
        self._transcript = bytearray()
        self._stderr = bytearray()
        self._condition = threading.Condition()
        self._qmp_lock = threading.Lock()
        self._stop = threading.Event()
        self.transcript_truncated = False
        self._transcript_file: Any = None

    def _validate_paths(self) -> None:
        required = {
            "QEMU executable": self.qemu,
            "OVMF code": self.firmware_code,
            "boot image": self.boot_image,
        }
        if self.firmware_vars:
            required["OVMF variables"] = self.firmware_vars
        if self.data_image:
            required["data image"] = self.data_image
        missing = [f"{label}: {path}" for label, path in required.items() if not path.is_file()]
        if missing:
            raise QemuHarnessError("missing harness input(s): " + ", ".join(missing))

    def build_command(self, serial_port: int, qmp_port: int) -> list[str]:
        """Build the deterministic one-vCPU PC/i440fx command line."""
        command = [
            str(self.qemu), "-machine", "pc,accel=tcg", "-cpu", "qemu64",
            "-smp", "1", "-m", "128M", "-display", "none", "-monitor", "none",
            "-no-reboot", "-no-shutdown",
            "-drive", f"if=pflash,format=raw,readonly=on,file={self.firmware_code}",
        ]
        if self.firmware_vars:
            command.extend(["-drive", f"if=pflash,format=raw,file={self.firmware_vars}"])
        command.extend([
            "-drive", f"if=ide,index=0,media=disk,format=raw,file={self.boot_image}",
            "-chardev", f"socket,id=serial0,host=127.0.0.1,port={serial_port},server=on,wait=off",
            "-device", "isa-serial,chardev=serial0",
            "-qmp", f"tcp:127.0.0.1:{qmp_port},server=on,wait=off",
        ])
        if self.data_image:
            command.extend(["-drive", f"if=ide,index=1,media=disk,format=raw,file={self.data_image}"])
        command.extend(self.extra_args)
        return command

    def start(self) -> "QemuHarness":
        if self.process is not None:
            raise QemuHarnessError("harness has already been started")
        self._validate_paths()
        serial_port, qmp_port = _free_tcp_port(), _free_tcp_port()
        if serial_port == qmp_port:
            qmp_port = _free_tcp_port()
        if self.transcript_path:
            self.transcript_path.parent.mkdir(parents=True, exist_ok=True)
            self._transcript_file = self.transcript_path.open("wb")
        self.process = subprocess.Popen(
            self.build_command(serial_port, qmp_port),
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        deadline = time.monotonic() + self.timeout
        try:
            self._serial = self._connect(serial_port, deadline, "serial")
            self._qmp = self._connect(qmp_port, deadline, "QMP")
            self._qmp.settimeout(0.2)
            self._receive_qmp(deadline)  # server greeting
            self._qmp_execute("qmp_capabilities", deadline=deadline)
        except Exception:
            self.stop()
            raise
        threading.Thread(target=self._read_serial, name="qemu-serial", daemon=True).start()
        threading.Thread(target=self._read_stderr, name="qemu-stderr", daemon=True).start()
        return self

    def _connect(self, port: int, deadline: float, label: str) -> socket.socket:
        last_error: OSError | None = None
        while time.monotonic() < deadline:
            self._raise_if_exited(f"opening {label}")
            connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            connection.settimeout(min(0.2, max(0.01, deadline - time.monotonic())))
            try:
                connection.connect(("127.0.0.1", port))
                return connection
            except OSError as error:
                last_error = error
                connection.close()
                time.sleep(0.02)
        raise QemuTimeoutError(f"timed out opening {label}: {last_error}")

    def _read_serial(self) -> None:
        assert self._serial is not None
        self._serial.settimeout(0.2)
        while not self._stop.is_set():
            try:
                chunk = self._serial.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not chunk:
                break
            with self._condition:
                self._transcript.extend(chunk)
                overflow = len(self._transcript) - self.max_transcript_bytes
                if overflow > 0:
                    del self._transcript[:overflow]
                    self.transcript_truncated = True
                if self._transcript_file:
                    self._transcript_file.write(chunk)
                    self._transcript_file.flush()
                self._condition.notify_all()

    def _read_stderr(self) -> None:
        assert self.process is not None and self.process.stderr is not None
        while not self._stop.is_set():
            chunk = self.process.stderr.read(4096)
            if not chunk:
                break
            self._stderr.extend(chunk)
            overflow = len(self._stderr) - 64 * 1024
            if overflow > 0:
                del self._stderr[:overflow]

    @property
    def transcript(self) -> bytes:
        with self._condition:
            return bytes(self._transcript)

    @property
    def transcript_text(self) -> str:
        return self.transcript.decode("utf-8", errors="replace")

    def wait_for(self, expected: str | bytes, timeout: float | None = None) -> bytes:
        needle = expected.encode() if isinstance(expected, str) else expected
        if not needle:
            raise ValueError("expected transcript fragment must not be empty")
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        with self._condition:
            while needle not in self._transcript:
                self._raise_if_exited(f"waiting for {needle!r}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    tail = bytes(self._transcript).decode("utf-8", errors="replace")[-1000:]
                    raise QemuTimeoutError(
                        f"timed out waiting for {needle!r}; transcript tail={tail!r}"
                    )
                self._condition.wait(min(remaining, 0.1))
            return bytes(self._transcript)

    def _receive_qmp(self, deadline: float) -> dict[str, Any]:
        assert self._qmp is not None
        while time.monotonic() < deadline:
            newline = self._qmp_buffer.find(b"\n")
            if newline >= 0:
                raw = bytes(self._qmp_buffer[:newline]).strip()
                del self._qmp_buffer[: newline + 1]
                if raw:
                    try:
                        value = json.loads(raw)
                    except json.JSONDecodeError as error:
                        raise QmpError(f"invalid QMP response: {raw!r}") from error
                    if isinstance(value, dict):
                        return value
            try:
                chunk = self._qmp.recv(4096)
            except socket.timeout:
                self._raise_if_exited("waiting for QMP")
                continue
            if not chunk:
                raise QmpError("QMP connection closed")
            self._qmp_buffer.extend(chunk)
        raise QemuTimeoutError("timed out waiting for QMP response")

    def _qmp_execute(
        self, command: str, arguments: dict[str, Any] | None = None, *, deadline: float | None = None
    ) -> Any:
        if self._qmp is None:
            raise QmpError("QMP is not connected")
        end = deadline if deadline is not None else time.monotonic() + self.timeout
        request: dict[str, Any] = {"execute": command}
        if arguments:
            request["arguments"] = arguments
        with self._qmp_lock:
            self._qmp.sendall(json.dumps(request, separators=(",", ":")).encode() + b"\n")
            while True:
                response = self._receive_qmp(end)
                if "event" in response:
                    continue
                if "error" in response:
                    raise QmpError(f"QMP {command!r} failed: {response['error']}")
                if "return" in response:
                    return response["return"]

    def send_key(self, qcode: str, *, shifted: bool = False, hold_ms: int = 50) -> None:
        keys = []
        if shifted:
            keys.append({"type": "qcode", "data": "shift"})
        keys.append({"type": "qcode", "data": qcode})
        self._qmp_execute("send-key", {"keys": keys, "hold-time": hold_ms})

    def send_text(self, text: str) -> None:
        """Type printable ASCII through QMP as actual emulated keyboard input."""
        for character in text:
            if "a" <= character <= "z":
                self.send_key(character)
            elif "A" <= character <= "Z":
                self.send_key(character.lower(), shifted=True)
            elif character in _BASE_KEYS:
                self.send_key(_BASE_KEYS[character])
            elif character in _SHIFTED_KEYS:
                self.send_key(_BASE_KEYS[_SHIFTED_KEYS[character]], shifted=True)
            else:
                raise ValueError(f"unsupported keyboard character: {character!r}")

    def send_line(self, text: str) -> None:
        self.send_text(text)
        self.send_key("ret")

    def press_backspace(self) -> None:
        self.send_key("backspace")

    def _raise_if_exited(self, action: str) -> None:
        if self.process is not None and (code := self.process.poll()) is not None:
            details = bytes(self._stderr).decode(errors="replace")[-1000:]
            raise QemuExitedError(f"QEMU exited with code {code} while {action}; stderr tail={details!r}")

    def wait(self, timeout: float | None = None) -> int:
        if self.process is None:
            raise QemuHarnessError("harness has not been started")
        try:
            return self.process.wait(timeout=self.timeout if timeout is None else timeout)
        except subprocess.TimeoutExpired as error:
            raise QemuTimeoutError("timed out waiting for QEMU to exit") from error

    def stop(self) -> None:
        self._stop.set()
        process = self.process
        if process is not None and process.poll() is None:
            try:
                self._qmp_execute("quit", deadline=time.monotonic() + 1.0)
                process.wait(timeout=1.0)
            except (QemuHarnessError, OSError, subprocess.TimeoutExpired):
                process.terminate()
                try:
                    process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2.0)
        for connection in (self._serial, self._qmp):
            if connection:
                try:
                    connection.close()
                except OSError:
                    pass
        if self._transcript_file:
            self._transcript_file.close()
            self._transcript_file = None

    def __enter__(self) -> "QemuHarness":
        return self.start()

    def __exit__(self, *_: object) -> None:
        self.stop()


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", required=True, type=Path)
    parser.add_argument("--firmware-code", required=True, type=Path)
    parser.add_argument("--firmware-vars", type=Path)
    parser.add_argument("--boot-image", required=True, type=Path)
    parser.add_argument("--data-image", type=Path)
    parser.add_argument("--transcript", type=Path)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--expect", action="append", default=[])
    parser.add_argument("--send", action="append", default=[], help="text line to type after expectations")
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    with QemuHarness(
        qemu=args.qemu, firmware_code=args.firmware_code, firmware_vars=args.firmware_vars,
        boot_image=args.boot_image, data_image=args.data_image, timeout=args.timeout,
        transcript_path=args.transcript,
    ) as harness:
        for expected in args.expect:
            harness.wait_for(expected)
        for line in args.send:
            harness.send_line(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
