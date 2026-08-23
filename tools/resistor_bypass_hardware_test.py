"""Safely exercise the PC13/PC14 resistor-bypass AT interlocks."""

from __future__ import annotations

import argparse
import sys
import time
from collections.abc import Callable
from typing import Any

DEFAULT_PORT = "COM12"
DEFAULT_BAUD = 9600
DEFAULT_TIMEOUT_SECONDS = 1.5
READ_POLL_SECONDS = 0.01
CLEANUP_COMMANDS = (
    "AT+MOTOR_BYPASS=OFF",
    "AT+MOTOR=STOP",
    "AT+CHARGE_BYPASS=OFF",
    "AT+CHARGE=OFF",
)


def read_response(port: Any, timeout_seconds: float) -> bytes:
    """Read through the terminal OK or ERROR line."""
    response = bytearray()
    deadline = time.monotonic() + timeout_seconds

    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            response.extend(chunk)
            lines = bytes(response).splitlines()
            if any(line == b"OK" or line.startswith(b"ERROR:") for line in lines):
                return bytes(response)
        else:
            time.sleep(READ_POLL_SECONDS)
    return bytes(response)


def send_command(
    port: Any,
    command: str,
    expected_token: str = "OK",
    *,
    timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
) -> bytes:
    """Send one CRLF-terminated command and validate its response."""
    port.reset_input_buffer()
    port.write(command.encode("ascii") + b"\r\n")
    port.flush()
    response = read_response(port, timeout_seconds)
    printable = response.decode("utf-8", errors="backslashreplace").strip()
    print(f"{command}: {printable}")

    token = expected_token.encode("ascii")
    lines = response.splitlines()
    terminal = next(
        (line for line in lines if line == b"OK" or line.startswith(b"ERROR:")),
        None,
    )
    if terminal is None or token not in response:
        raise RuntimeError(
            f"{command} expected {expected_token!r}; raw={response.hex(' ')}"
        )
    return response


def run_default_sequence(port: Any) -> None:
    """Check communication and fail-closed behavior without energizing loads."""
    send_command(port, "AT+VERSION?", "+VERSION:")
    send_command(port, "AT+MOTOR=STOP")
    send_command(port, "AT+CHARGE=OFF")
    send_command(port, "AT+MOTOR_BYPASS=OFF")
    send_command(port, "AT+MOTOR_BYPASS=ON", "ERROR:STATE")
    send_command(port, "AT+CHARGE_BYPASS=OFF")
    send_command(port, "AT+CHARGE_BYPASS=ON", "ERROR:STATE")


def run_load_sequence(
    port: Any,
    *,
    settle_seconds: float,
    hold_seconds: float,
    sleep_fn: Callable[[float], None] = time.sleep,
) -> None:
    """Briefly exercise real motor and charge phases without fault injection."""
    send_command(port, "AT+MOTOR=FWD")
    sleep_fn(settle_seconds)
    send_command(port, "AT+MOTOR_BYPASS=ON")
    sleep_fn(hold_seconds)

    # A direction change must restore PC13 low; the new direction needs a new ON.
    send_command(port, "AT+MOTOR=REV")
    sleep_fn(settle_seconds)
    send_command(port, "AT+MOTOR_BYPASS=ON")
    sleep_fn(hold_seconds)
    send_command(port, "AT+MOTOR_BYPASS=OFF")
    send_command(port, "AT+MOTOR=STOP")

    send_command(port, "AT+CHARGE=ON")
    sleep_fn(settle_seconds)
    send_command(port, "AT+CHARGE_BYPASS=ON")
    sleep_fn(hold_seconds)
    send_command(port, "AT+CHARGE=OFF")


def cleanup(port: Any) -> None:
    """Best-effort cleanup that attempts every safe-state command."""
    for command in CLEANUP_COMMANDS:
        try:
            send_command(port, command)
        except (OSError, RuntimeError) as error:
            print(f"cleanup warning: {error}", file=sys.stderr)


def run(
    port_name: str,
    baud: int,
    exercise_loads: bool,
    settle_seconds: float,
    hold_seconds: float,
) -> None:
    """Open the adapter, run selected checks, and always force a safe state."""
    import serial

    with serial.Serial(
        port_name,
        baud,
        timeout=READ_POLL_SECONDS,
        write_timeout=DEFAULT_TIMEOUT_SECONDS,
    ) as port:
        try:
            run_default_sequence(port)
            if exercise_loads:
                run_load_sequence(
                    port,
                    settle_seconds=settle_seconds,
                    hold_seconds=hold_seconds,
                )
        finally:
            cleanup(port)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--exercise-loads", action="store_true")
    parser.add_argument("--settle-seconds", type=float, default=0.5)
    parser.add_argument("--hold-seconds", type=float, default=0.5)
    return parser.parse_args()


def main() -> int:
    options = parse_args()
    try:
        run(
            options.port,
            options.baud,
            options.exercise_loads,
            options.settle_seconds,
            options.hold_seconds,
        )
    except (ImportError, OSError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("PASS: resistor-bypass hardware sequence completed safely")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
