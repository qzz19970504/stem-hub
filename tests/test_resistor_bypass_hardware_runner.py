from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/resistor_bypass_hardware_test.py"


def load_runner():
    spec = importlib.util.spec_from_file_location("bypass_runner", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FakeSerial:
    def __init__(self, responses: dict[bytes, bytes]):
        self.responses = responses
        self.writes: list[bytes] = []
        self.pending = b""

    @property
    def in_waiting(self) -> int:
        return len(self.pending)

    def reset_input_buffer(self) -> None:
        self.pending = b""

    def write(self, payload: bytes) -> int:
        self.writes.append(payload)
        self.pending = self.responses[payload]
        return len(payload)

    def flush(self) -> None:
        return None

    def read(self, size: int) -> bytes:
        chunk, self.pending = self.pending[:size], self.pending[size:]
        return chunk


def test_send_command_frames_crlf_and_checks_terminal_token() -> None:
    runner = load_runner()
    port = FakeSerial({b"AT+MOTOR_BYPASS=OFF\r\n": b"OK\r\n"})

    response = runner.send_command(port, "AT+MOTOR_BYPASS=OFF", "OK")

    assert response == b"OK\r\n"
    assert port.writes == [b"AT+MOTOR_BYPASS=OFF\r\n"]


def test_default_sequence_only_checks_handshake_and_inactive_interlocks() -> None:
    runner = load_runner()
    responses = {
        b"AT+VERSION?\r\n": b"+VERSION:release-v3.2\r\nOK\r\n",
        b"AT+MOTOR=STOP\r\n": b"OK\r\n",
        b"AT+CHARGE=OFF\r\n": b"OK\r\n",
        b"AT+MOTOR_BYPASS=OFF\r\n": b"OK\r\n",
        b"AT+MOTOR_BYPASS=ON\r\n": b"ERROR:STATE\r\n",
        b"AT+CHARGE_BYPASS=OFF\r\n": b"OK\r\n",
        b"AT+CHARGE_BYPASS=ON\r\n": b"ERROR:STATE\r\n",
    }
    port = FakeSerial(responses)

    runner.run_default_sequence(port)

    assert b"AT+MOTOR=FWD\r\n" not in port.writes
    assert b"AT+CHARGE=ON\r\n" not in port.writes


def test_cleanup_order_forces_both_bypasses_and_loads_off() -> None:
    runner = load_runner()
    responses = {
        (command + "\r\n").encode("ascii"): b"OK\r\n"
        for command in runner.CLEANUP_COMMANDS
    }
    port = FakeSerial(responses)

    runner.cleanup(port)

    assert port.writes == [
        (command + "\r\n").encode("ascii")
        for command in runner.CLEANUP_COMMANDS
    ]
