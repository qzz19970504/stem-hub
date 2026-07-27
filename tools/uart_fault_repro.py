"""Exercise STM32 UART1 recovery with wrong-baud traffic and AT queries."""

from __future__ import annotations

import argparse
import re
import sys
import time

import serial


DEFAULT_BAUD = 115200
DEFAULT_FAULT_BAUD = 9600
DEFAULT_TIMEOUT_SECONDS = 1.0
READ_POLL_SECONDS = 0.01
QUIET_PERIOD_SECONDS = 0.08
STRESS_PROGRESS_SECONDS = 30.0
STRESS_LOOP_DELAY_SECONDS = 0.05
FAULT_PAYLOAD = bytes(range(256))
DIAG_ERROR_PATTERN = re.compile(rb"\bRX_ERR=(\d+)")


def read_response(
    port: serial.Serial,
    *,
    timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
) -> bytes:
    """Read one CRLF-framed AT response until terminal status or timeout."""
    response = bytearray()
    deadline = time.monotonic() + timeout_seconds
    last_byte_time = time.monotonic()

    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            response.extend(chunk)
            last_byte_time = time.monotonic()
            if response.endswith(b"\r\nOK\r\n") or b"\r\nERROR:" in response:
                break
            continue
        if response and time.monotonic() - last_byte_time >= QUIET_PERIOD_SECONDS:
            break
        time.sleep(READ_POLL_SECONDS)
    return bytes(response)


def query(
    port: serial.Serial,
    command: bytes,
    *,
    should_print: bool = True,
) -> bytes:
    """Send one AT command and return its complete raw response."""
    port.reset_input_buffer()
    port.write(command + b"\r\n")
    port.flush()
    response = read_response(port)
    if should_print:
        print(
            f"{command.decode('ascii')}: "
            f"{response.decode('utf-8', errors='backslashreplace').strip()}"
        )
    return response


def inject_wrong_baud_traffic(
    port: serial.Serial,
    *,
    fault_baud: int,
) -> None:
    """Transmit deterministic bytes at a baud rate the MCU does not expect."""
    port.baudrate = fault_baud
    port.write(FAULT_PAYLOAD)
    port.flush()
    port.baudrate = DEFAULT_BAUD
    time.sleep(QUIET_PERIOD_SECONDS)
    port.write(b"\r\n\r\n")
    port.flush()
    time.sleep(QUIET_PERIOD_SECONDS)
    port.reset_input_buffer()


def require_ok(response: bytes, command: str) -> None:
    """Exit with a useful failure when a command has no terminal OK."""
    if response.endswith(b"\r\nOK\r\n"):
        return
    raise RuntimeError(
        f"{command} did not return a complete OK response; raw={response.hex(' ')}"
    )


def run_stress(port: serial.Serial, stress_seconds: float) -> None:
    """Run mixed host-style queries for the requested duration."""
    if stress_seconds <= 0:
        return

    commands = (b"AT+SENSE?", b"AT+FAULT?", b"AT+MOTOR?")
    deadline = time.monotonic() + stress_seconds
    next_progress = time.monotonic() + STRESS_PROGRESS_SECONDS
    response_count = 0
    last_sense = b""

    while time.monotonic() < deadline:
        for command in commands:
            response = query(port, command, should_print=False)
            require_ok(response, command.decode("ascii"))
            response_count += 1
            if command == b"AT+SENSE?":
                last_sense = response

        if response_count % 30 == 0:
            diag = query(port, b"AT+DIAG?", should_print=False)
            require_ok(diag, "AT+DIAG?")
            response_count += 1

        if time.monotonic() >= next_progress:
            remaining = max(0.0, deadline - time.monotonic())
            print(
                f"STRESS: {response_count} complete responses, "
                f"{remaining:.0f}s remaining"
            )
            next_progress += STRESS_PROGRESS_SECONDS
        time.sleep(STRESS_LOOP_DELAY_SECONDS)

    print(f"STRESS PASS: {response_count} complete responses")
    print(
        "LAST SENSE: "
        + last_sense.decode("utf-8", errors="backslashreplace").strip()
    )


def run(
    port_name: str,
    fault_baud: int,
    fault_cycles: int,
    stress_seconds: float,
) -> None:
    """Run the connected-device UART recovery regression."""
    with serial.Serial(
        port_name,
        DEFAULT_BAUD,
        timeout=READ_POLL_SECONDS,
        write_timeout=DEFAULT_TIMEOUT_SECONDS,
    ) as port:
        require_ok(query(port, b"AT+VERSION?"), "AT+VERSION?")

        for cycle_index in range(fault_cycles):
            inject_wrong_baud_traffic(port, fault_baud=fault_baud)
            response = query(port, b"AT+VERSION?")
            require_ok(response, f"AT+VERSION? after fault cycle {cycle_index + 1}")

        diag = query(port, b"AT+DIAG?")
        require_ok(diag, "AT+DIAG?")
        match = DIAG_ERROR_PATTERN.search(diag)
        if match is None or int(match.group(1)) == 0:
            raise RuntimeError(
                "fault traffic did not increment RX_ERR; the recovery path was "
                "not exercised"
            )
        print(f"PASS: MCU recovered from {match.group(1).decode()} UART errors")
        run_stress(port, stress_seconds)

        final_diag = query(port, b"AT+DIAG?")
        require_ok(final_diag, "final AT+DIAG?")


def parse_args() -> argparse.Namespace:
    """Parse command-line options for the connected test fixture."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM12")
    parser.add_argument("--fault-baud", type=int, default=DEFAULT_FAULT_BAUD)
    parser.add_argument("--fault-cycles", type=int, default=8)
    parser.add_argument("--stress-seconds", type=float, default=0.0)
    return parser.parse_args()


def main() -> int:
    """Run the regression and return a process-compatible status code."""
    options = parse_args()
    try:
        run(
            options.port,
            options.fault_baud,
            options.fault_cycles,
            options.stress_seconds,
        )
    except (OSError, RuntimeError, serial.SerialException) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
