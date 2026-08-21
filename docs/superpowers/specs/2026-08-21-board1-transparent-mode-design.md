# Board 1 Exclusive Transparent Mode Design

## Goal

Make board 1 use the same transparent-transfer contract as board 2: UART1 is
either an AT command channel or a raw forward channel, never both at once.
The existing motor, power, sensing, thermal-protection, and stall-protection
tasks remain independent of the communication mode.

## Protocol

- `AT+TRANS=1\r\n` enters UART1-to-UART2 transparent mode.
- `AT+TRANS=2\r\n` enters UART1-to-UART3 transparent mode.
- `AT+TRANS=1&2\r\n` enters dual-target transparent mode.
- A valid entry command returns `OK\r\n` before raw forwarding begins.
- The old `AT+UART2=ON/OFF`, `AT+UART3=ON/OFF`,
  `AT+UART2&3=ON/OFF`, and `AT+UART23=ON/OFF` forms are unsupported.
- AT mode does not forward non-AT lines. Transparent mode does not invoke the
  AT parser, so AT-looking text and arbitrary binary bytes are payload.
- `AT+UARTTX=<HEX>` remains parseable for compatibility. Because no bridge
  target is active in AT mode, it returns `ERROR:UART_DISABLED`; in transparent
  mode the complete text is forwarded as ordinary payload.

## State and Data Flow

The AT task owns one communication state.

1. In AT mode, UART1 chunks feed the CRLF line reader and existing AT command
   handler. A successful `AT+TRANS` selects exactly one target mask and enters
   transparent mode. Any bytes after that command in the same receive chunk
   are immediately treated as transparent payload.
2. In transparent mode, each UART1 receive chunk goes directly to the escape
   detector. Forwardable bytes are sent unchanged to UART2, UART3, or both.
3. A valid escape clears both targets, flushes downstream receive queues,
   returns to AT mode, and sends `OK\r\n` on UART1.
4. UART2/UART3 input keeps the existing board-1 behavior: while its target is
   active, the bridge task publishes `+UART2RX:<HEX>\r\n` or
   `+UART3RX:<HEX>\r\n` on UART1. This matches board 2 and avoids changing the
   host-facing reverse-channel protocol.

Bridge target selection, clearing, queue flushing, and bridge-task draining
are serialized with one bridge mutex so an event cannot be published using a
stale enabled state during entry or exit.

## Guarded Escape

UART1 changes from one-byte interrupt reception to interrupt-to-idle reception
with 64-byte chunks. UART2 and UART3 retain their existing one-byte interrupt
reception to minimize impact on board-1 diagnostics and other tasks.

At 9600 8N1, the hardware UART idle event occurs after one full frame time
(approximately 1.04 ms), which satisfies the required 1 ms guard interval.
Each UART1 chunk records whether it began and ended after an idle interval.
Only exactly three plus bytes with both guards exit transparent mode.

Candidate plus bytes are held in a three-byte buffer. If either guard is
missing, the sequence is embedded in other data, a fourth plus arrives, or a
later byte disproves the candidate, every held byte is replayed in original
order. Examples such as `abc+++def`, `++++`, fragmented plus bytes, and binary
payload must never lose, duplicate, or reorder bytes.

UART1 ring or chunk-queue overflow aborts transparent mode, clears targets,
resets the AT line reader, and reports `ERROR:RX_OVERFLOW\r\n`. This avoids
continuing after the byte stream has become incomplete.

## Other-Function Impact

Entering transparent mode is allowed while the motor or charging state machine
is active, matching board 2. During transparent mode, manual motor, power,
configuration, and query AT commands are unavailable until guarded `+++` exits
the mode.

This does not disable automatic safety behavior. The sensor task continues to
evaluate thermal limits, the motor task continues its 10 ms stall-current
monitoring, and their existing queues can still force thermal shutdown or motor
braking without AT-task participation. No motor, output, ADC, persistent
configuration, or protection API changes are part of this work.

The AT task's new state and receive buffer are static rather than stack-local.
Debug and Release links must prove the additions fit the board's limited RAM
and Flash, and existing stack high-water diagnostics remain available.

## Verification

Host tests cover the three new mappings, rejection of every old spelling,
unchanged parsing of all non-bridge commands, line-reader behavior, chunk
metadata overflow, guarded escape, failed-candidate replay, binary payload,
target re-entry, and state-selection semantics. All existing board-1 native C
tests and Python contract tests must continue to pass.

Both Debug and Release firmware builds must fit 20 KiB RAM and 63 KiB Flash.
Hardware verification programs and verifies the Debug ELF through the detected
ST-Link, then uses UART1 at 9600 8N1 to test command entry, AT suppression,
lossless raw forwarding, invalid and valid escapes, return to AT mode, and
unaffected representative AT queries. The currently connected CH340 COM10 has
already been identified as board-1 UART3; UART2 is verified after the adapter is
moved, and dual-target mode is observed on both physical outputs before merge.
