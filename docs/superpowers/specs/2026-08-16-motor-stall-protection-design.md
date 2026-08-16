# Motor Stall Protection Design

**Date:** 2026-08-16

**Status:** Approved design, pending written-spec review

**Scope:** STM32F103C8 firmware motor-current conversion, stall shutdown, AT configuration, nonvolatile storage, hardware verification, and user documentation

## 1. Context and goals

The DC planetary gearmotor draws about 0.5 A with no load, about 3.2 A at rated load, and has a stated 19 A stall current. The board senses motor current through the DRV8874 IPROPI output on ADC2 IN8. Schematic evidence confirms that R19, the IPROPI resistor, is 220 ohms.

The current firmware incorrectly models R19 as 2.5 kohms and clamps telemetry at 2.9 A. The feature must first correct that conversion, then stop the motor when current remains above a configurable threshold. The threshold must be adjustable over the existing UART1 RS-485 AT interface and survive MCU resets and power cycles.

The accepted default policy is deliberately simple: current magnitude plus persistence time. Current slew rate is not part of this revision because it would require another calibration parameter and would be more sensitive to normal startup and load transients.

## 2. Accepted behavior

### 2.1 Current conversion

Use the DRV8874 typical current-mirror ratio of 450 microamps per amp and the fitted R19 value of 220 ohms:

```text
V_IPROPI = I_MOTOR * 450 uA/A * 220 ohm
I_MOTOR_mA = V_IPROPI_mV * 1000 / 99
```

At a 3.3 V ADC reference, the measurable range is approximately 0 to 33.3 A. Representative values are:

| Motor current | IPROPI voltage | Approximate 12-bit ADC count |
| ---: | ---: | ---: |
| 0.5 A | 49.5 mV | 61 |
| 3.2 A | 316.8 mV | 393 |
| 19 A | 1.881 V | 2334 |

`AT+MOTOR?` continues to report integer milliamps. `AT+SENSE?` continues to report one decimal place, but its clamp changes from the incorrect 2.9 A limit to the physical 33.3 A range.

### 2.2 Stall detector

The motor task remains the sole owner of DRV8874 mode changes and motor-current monitoring. A small pure-logic guard holds the timing state so the policy can be unit tested without HAL or RTOS dependencies.

When FWD or REV is applied, the detector begins a 300 ms startup blanking interval. A direction change completes the existing brake dead time and then begins a fresh blanking interval. During blanking, current is measured for status reporting but cannot trip the software stall detector.

After blanking, ADC2 IN8 is sampled every 10 ms:

- A sample below the configured threshold clears the pending over-threshold interval.
- A sample equal to or above the threshold starts or continues the interval.
- When the current has remained equal to or above the threshold for at least 100 ms, the detector reports a stall.
- An ADC read failure clears the pending interval, does not synthesize a stall, and leaves the motor in its existing mode.
- Tick comparisons use subtraction-based wrap-safe arithmetic.

The response time after blanking is therefore approximately 100 to 110 ms, depending on task scheduling.

### 2.3 Stall response and restart

On a stall, the motor task keeps nSLEEP high, drives EN low, preserves PH, stores the trip current, changes the reported mode to BRAKE, and sets the existing `overcurrent_latched` field. The existing `AT+MOTOR?` response remains compatible and reports `MODE=BRAKE,OVERCURRENT=1`.

The latch is diagnostic, not a direction interlock:

- STOP, BRAKE, and SLEEP clear detector timing but retain the most recent stall flag.
- A subsequent FWD or REV command is allowed in either direction.
- Applying FWD or REV clears the old stall flag and starts a new blanking and monitoring cycle.
- If the obstruction remains, the same direction trips again after blanking plus persistence time.

Thermal protection remains higher priority. While thermal protection is active, the consumer task forces SLEEP and existing command guards continue to reject unsafe motor starts.

## 3. AT protocol

### 3.1 Commands

Set and persist the threshold in milliamps:

```text
AT+STALL_CURRENT=4000\r\n
OK\r\n
```

Query the active threshold:

```text
AT+STALL_CURRENT=?\r\n
+STALL_CURRENT:4000\r\n
OK\r\n
```

The accepted range is 1000 through 30000 mA inclusive. The power-on default is 4000 mA.

Malformed numbers, signs, suffixes, overflow, and out-of-range values are rejected by the strict parser and follow the existing `ERROR:INVALID_COMMAND` behavior. A set request while the motor is in FWD or REV returns `ERROR:MOTOR_RUNNING`. A persistence failure returns `ERROR:FLASH_WRITE`. Repeating the active value returns `OK` without erasing Flash.

The firmware handshake remains `+VERSION:release-v3.2` followed by `OK`. Existing command and response formats are unchanged; the new commands are additive.

### 3.2 Command flow

The AT task parses and validates the numeric value, then checks motor status. For a permitted change, it persists the new value first and publishes it to shared application state only after successful Flash verification. The query path reads the shared active value. The motor task snapshots the latest threshold each monitoring cycle, so an idle configuration change takes effect on the next motor start.

## 4. Nonvolatile storage

The STM32F103C8 linker currently exposes all 64 KiB of Flash to the program. Reserve the final 1 KiB page by reducing the executable Flash region to 63 KiB. The configuration page begins at `0x0800FC00`. This address must be defined once in the storage module and documented next to the linker reservation.

The stored record is 16 bytes:

```c
typedef struct
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t reserved;
    uint32_t stall_current_ma;
    uint32_t crc32;
} AppStallConfigRecord;
```

The CRC covers all fields before `crc32`. On boot, the loader accepts only records with the expected magic, format version, reserved value, CRC, and threshold range. An erased, corrupt, incompatible, or interrupted record falls back to 4000 mA.

The writer unlocks Flash, erases exactly the reserved page, programs the record using STM32F1-supported halfword writes, locks Flash on every exit path, and validates the stored record by reading it back. Flash updates are allowed only while the motor is not running. A power loss during erase or programming can discard the customized threshold, but the next boot safely falls back to 4000 mA; it cannot load an unchecked partial value.

The current Debug binary is approximately 56.1 KiB, leaving about 8 KiB of executable headroom after the page reservation. Both Debug and Release links must prove that the reservation still fits.

## 5. Module boundaries

- `app_motor_current`: converts IPROPI millivolts to milliamps and clamps display values to the corrected physical range.
- `app_motor_stall_guard`: owns blanking and persistence timing; contains no HAL or RTOS calls.
- `app_stall_config`: validates records, calculates CRC, loads the default or persisted threshold, and performs the STM32 Flash transaction through a narrow hardware boundary.
- `app_state`: stores the active threshold alongside existing shared motor state.
- `app_motor_task`: owns sampling, feeds the guard, applies BRAKE, and maintains status.
- `app_at_protocol` and `app_at_task`: parse, execute, and reply to the two additive AT commands.

This keeps policy, conversion, storage, transport, and hardware actuation independently understandable and testable.

## 6. Error handling and safety

- Invalid persisted data always selects the conservative 4000 mA default.
- Failed Flash erase, program, or verification leaves the runtime threshold unchanged and returns an explicit AT error.
- Configuration writes are rejected while the motor runs, avoiding a Flash erase stall during active protection.
- ADC failures never create a false stall but do break the required continuous-overcurrent evidence.
- A detected stall always actuates BRAKE in the motor owner task; no other task writes the motor pins.
- Thermal forced-safe behavior and power-path interlocks are unchanged.
- No test intentionally produces the motor's theoretical 19 A stall current.

## 7. Verification strategy

### 7.1 Automated tests

Test-first native C coverage will include:

- Real production conversion at 0 A, 0.5 A, 3.2 A, 19 A, and ADC full scale.
- Startup blanking boundaries, short spikes, continuous 100 ms over-threshold detection, reset on a low sample or failed read, restart behavior, and tick wrap.
- Strict set/query AT parsing, lower and upper bounds, malformed values, and overflow.
- Configuration record validation, CRC corruption, format mismatch, range mismatch, erased Flash fallback, duplicate-value suppression, and storage error propagation.
- Motor-task integration through narrow callbacks or source-contract tests where HAL ownership cannot be linked natively.

All existing firmware native C tests and Python contract tests must also pass. Debug and Release firmware builds must complete without overflow.

### 7.2 Hardware tests

Use STM32CubeProgrammer 2.23.0 and ST-Link serial `37FF71064E573436947D1143` to program, verify, and reset the built ELF under SWD reset mode.

Over UART1 RS-485:

1. Confirm `AT+VERSION?`, `AT+STALL_CURRENT=?`, and `AT+MOTOR?` responses.
2. Set a non-default threshold while stopped, reset or power-cycle the MCU, and confirm the value survived.
3. Confirm a set request is rejected during FWD or REV.
4. Confirm normal no-load and available controlled-load operation do not trip at 4000 mA.
5. Use a lower allowed test threshold plus a controlled load to cross the threshold without forcing a 19 A hard stall; verify BRAKE and `OVERCURRENT=1`.
6. Send a new FWD or REV command and verify restart is permitted; continued excess current must trip again.
7. Restore and persist 4000 mA before handoff.

If the connected setup cannot safely produce more than the minimum configurable 1000 mA, the hardware record must explicitly distinguish the verified AT/persistence path from the unverified physical trip path instead of claiming an end-to-end stall test.

## 8. Git and documentation delivery

Work occurs in the isolated `codex/motor-stall-protection` branch. The user's existing `.settings` modifications on `master` are excluded from every feature commit.

Update:

- `README.md` for corrected current sensing, protection behavior, build/test steps, and AT examples.
- `上位机AT命令文档.md` for command grammar, responses, errors, timing, restart behavior, and persistence.
- `钻杆mcu控制功能.md` for the implemented v3.2 incremental behavior and safety limits.
- A hardware test record under `docs/` with exact firmware, probe, RS-485 commands, observed responses, and any unverified physical step.

After automated and hardware verification, merge the feature branch into `master`, re-run relevant verification on the merge result, and create the final master commit without staging unrelated `.settings` changes.
