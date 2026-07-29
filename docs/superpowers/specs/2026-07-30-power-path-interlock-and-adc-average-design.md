# Power Path Interlock and ADC Rolling Average Design

## Scope

This change coordinates the STM32 firmware in `D:\Codes\STM32\stem-hub`
and the PySide6 host application in `D:\Codes\STM32\stem-hub-host`.

It replaces independent LM51770 and MP4317 control with a firmware-owned,
three-state power-path controller, adapts the host application to the new AT
contract, smooths the five periodic sensor ADC channels with a five-cycle
rolling average, updates user documentation, rebuilds both products, and
verifies the result on the connected target through ST-Link and UART1 on
COM12.

## Power-Path Invariant

The firmware permits exactly three stable states:

| State | LM51770 | MP4317 | Meaning |
|---|---:|---:|---|
| Off | Off | Off | Both power paths disabled |
| Charge | On | Off | Battery charging path enabled |
| Drive | Off | On | Downstream drive/output path enabled |

No accepted AT command may leave both devices enabled. Every transition to
Charge or Drive first writes both active-low enable pins to their disabled
levels, updates the stored I/O state, and only then enables the selected
device.

## AT Command Contract

The firmware accepts exactly these five power-path control commands:

| Command | Result |
|---|---|
| `AT+CHARGE=ON\r\n` | Disable both devices, then enable LM51770 |
| `AT+CHARGE=OFF\r\n` | Disable both devices |
| `AT+DRIVE=ON\r\n` | Disable both devices, then enable MP4317 |
| `AT+DRIVE=OFF\r\n` | Disable both devices |
| `AT+POWER=OFF\r\n` | Disable both devices |

Each accepted command returns `OK` after its complete power-mode request has
been queued. Queue failure returns `ERROR:OUTPUT_QUEUE`.

The following commands are removed from the parser and return
`ERROR:BAD_COMMAND`:

- `AT+LM51770=ON\r\n`
- `AT+LM51770=OFF\r\n`
- `AT+MP4317=ON\r\n`
- `AT+MP4317=OFF\r\n`
- `AT+POWER=ON\r\n`

The protocol intentionally has no independent chip-enable operation.

## Firmware Architecture

`AppAtCommandType` gains one power-mode command carrying an
`AppPowerMode` value: `OFF`, `CHARGE`, or `DRIVE`. Both `CHARGE=OFF` and
`DRIVE=OFF`, as well as `POWER=OFF`, parse to `OFF`.

The existing output queue remains the single route to the GPIO-owning output
task. `App_OutputEnqueuePowerMode()` places one complete mode request on that
queue. The output task applies the request as one indivisible queue item:

1. drive LM51770 disabled;
2. drive MP4317 disabled;
3. update both stored states to disabled;
4. when requested, enable exactly one selected path and update its state.

NMOS1 and NMOS2 retain their existing independent output requests. Direct
GPIO writes from the AT task are not introduced.

The firmware version advances because removing commands is an AT contract
change.

## Five-Cycle ADC Rolling Average

The rolling average covers the five ADC channels acquired by the 1 Hz sensor
task:

- battery NTC (`ADC1 IN4`);
- battery voltage (`ADC1 IN5`);
- NTC3 (`ADC2 IN6`);
- NTC2 (`ADC2 IN7`);
- NTC1 (`ADC2 IN9`).

Each channel owns a fixed five-element `uint16_t` ring and a `uint32_t`
running sum in static storage. The filter replaces the oldest sample after
the window fills, so each cycle adds constant work and performs no additional
ADC conversions. The maximum sum is `5 * 4095 = 20475`, well below the
`uint32_t` limit.

During startup, the mean uses the number of samples currently available.
Thus the first complete sensor cycle still publishes data, and the fifth
successful cycle establishes the full rolling window.

Filtering occurs on raw ADC counts before conversion to millivolts and
physical units. If a channel read fails, that channel's filter is not
advanced and the existing behavior of rejecting the incomplete snapshot is
retained.

Filter rings and sums use static storage, not the 1024-byte sensor task
stack. The implementation must not add a five-snapshot automatic array or
recursive call. Hardware verification records `STK_SENSOR` from
`AT+SENSE?` to confirm healthy remaining stack.

Motor current is excluded from this rolling filter. Its instantaneous ADC
sample participates in overcurrent protection, so averaging it would delay
the safety response. `MOTOR_I` therefore retains its current protection
semantics.

## Host Application Changes

The host protocol module replaces LM51770/MP4317 builders with:

- `cmd_set_charge(on)`;
- `cmd_set_drive(on)`;
- `cmd_power_off()`.

The UI names the downstream mode `DRIVE` instead of `DISCHARGE`. Charge and
Drive controls remain visually exclusive, while the firmware is the final
authority enforcing the invariant.

Each charge-mode transition sends one AT command rather than a three-command
chip sequence. Rapid user changes remain serialized through the existing
FIFO command/acknowledgement flow.

The global ALL OFF action sends `AT+POWER=OFF` for the two power-path devices,
then continues to disable NMOS1, NMOS2, and LED using their existing
commands.

The fake firmware models one power mode and never represents both devices as
enabled. Tests and documentation use the new command vocabulary exclusively.

## Error Handling

- Malformed or removed AT commands return `ERROR:BAD_COMMAND`.
- A full output queue returns `ERROR:OUTPUT_QUEUE`; no partial transition has
  occurred because a transition occupies one queue item.
- A failed sensor ADC read increments the existing diagnostic counter,
  leaves that channel's filter unchanged, and prevents publication of an
  incomplete snapshot.
- Host command errors restore/clear pending UI state through the existing
  output failure path.

## Verification

Firmware verification includes:

1. native parser tests for all five accepted commands and all removed or
   invalid forms;
2. native tests for the rolling accumulator, including startup, wraparound,
   and maximum ADC values;
3. native tests or HAL-backed seams proving each requested power mode writes
   the safe-off state before enabling one path;
4. a complete Debug firmware build with memory-usage inspection;
5. ST-Link programming of the resulting ELF;
6. COM12 checks that new commands return `OK`, old commands return
   `ERROR:BAD_COMMAND`, and repeated `AT+SENSE?` responses remain healthy;
7. GPIO output-data register inspection through ST-Link, when supported
   without disrupting UART execution, to confirm active-low PA8/PB3 levels
   for Off, Charge, and Drive;
8. `STK_SENSOR` inspection after the five-sample window is full.

Host verification includes targeted protocol/controller/fake-firmware/UI
tests, the complete pytest suite, Python compilation, a PyInstaller rebuild
using `env\release`, and a packaged executable smoke launch.

## Git Integration

Both repositories use branch `codex/power-path-interlock`.

The MCU branch starts from the current firmware HEAD because it contains the
v2.1/v2.2 power-control, UART, and motor-current work that has not yet reached
MCU `master`. After all verification succeeds, the feature branch is merged
into MCU `master`, bringing that required ancestry with it.

The host branch starts from host `master` and is merged back into host
`master` only after its complete verification passes.

The pre-existing line-ending-only changes in the MCU `.settings` bundle
files are not committed as part of this feature.
