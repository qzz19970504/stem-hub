# Power Resistor Bypass Control Design

**Date:** 2026-08-23

**Status:** Approved design, pending written-spec review

**Scope:** Restore the simultaneous AT/RS-485 firmware baseline at 9600 baud, add safety-interlocked PC13 and PC14 resistor-bypass controls, restore the host application to its 9600-baud baseline, document the protocol, and verify on hardware.

## 1. Context and goals

The exclusive transparent-mode firmware prevents the product from operating the motor while maintaining normal AT communication. This revision returns to firmware commit `4b5eb84`, where USART1, USART2, and USART3 all run at 9600 baud and the existing AT-controlled UART bridge remains available without replacing the AT command mode. The exclusive transparent-mode history remains preserved on `codex/transparent-mode`.

Two new board outputs control resistor bypass MOSFETs:

- PC13 (STM32F103C8T6 pin 2) bypasses the motor startup-current limiting resistor. Low inserts the resistor; high shorts it.
- PC14 (STM32F103C8T6 pin 3) bypasses the charge-current limiting cement resistors. Low selects precharge current; high selects full-power charging.

Both outputs must power up low. A command may remove a resistor only after the corresponding load has entered a state in which removal is safe. Firmware safety transitions must restore the low level automatically so a previous command cannot make a later motor start or charge phase begin at full current.

## 2. AT protocol

The two additive command families are:

```text
AT+MOTOR_BYPASS=ON\r\n
AT+MOTOR_BYPASS=OFF\r\n
AT+CHARGE_BYPASS=ON\r\n
AT+CHARGE_BYPASS=OFF\r\n
```

`ON` always means GPIO high and resistor bypassed. `OFF` always means GPIO low and resistor inserted. Valid requests return `OK\r\n`. An `ON` request made outside the permitted operating state returns `ERROR:STATE\r\n` and leaves the pin low. `OFF` remains valid in every state so the host can always request the conservative condition.

The parser remains strict: commands are uppercase, contain no spaces, and end in CRLF. Malformed names or values follow the existing parse-error behavior. No persistence or query command is added: both outputs return to low after reset and their safe-state transitions are automatic.

## 3. Motor bypass behavior (PC13)

The motor task is the sole owner of PC13. The AT task parses the request and uses the existing motor state to reject an obviously invalid `ON` request. The motor task validates the state again when applying the queued request, so a concurrent stop cannot leave PC13 high.

PC13 may become high only while the applied motor mode is `FWD` or `REV`. It is forced low:

- during GPIO initialization and runtime safe initialization;
- before entering `SLEEP`, `WAKE`, `BRAKE`, or `STOP`;
- when the stall guard brakes the motor;
- before an `FWD` to `REV` or `REV` to `FWD` transition disables and reverses the bridge;
- before any other forced-safe motor shutdown.

A direction change is a new motor startup. After the existing reversal dead time and new direction application, PC13 stays low until the host sends a new `AT+MOTOR_BYPASS=ON`. Repeating the same already-applied running direction does not by itself create a new startup transition.

## 4. Charge bypass behavior (PC14)

The existing output task is the sole owner of PC14 because it also owns the charge-cycle and thermal shutdown paths. PC14 may become high only during the actual ON phase of `APP_POWER_MODE_CHARGE`, when the LM51770 charge path is enabled.

PC14 is forced low:

- during GPIO initialization and runtime safe initialization;
- when the charge cycle reaches its periodic OFF phase;
- when the charge mode is stopped;
- when the system switches to drive mode or power-off mode;
- before thermal protection shuts down the charge path;
- before a subsequent charge ON phase starts.

Each charge ON phase therefore begins with the cement resistors inserted. The host may explicitly request full power after that phase begins. If the configured charge ON time is 60 seconds, PC14 may remain high until an explicit OFF request, a mode transition, or a safety shutdown because that configuration has no periodic OFF phase.

## 5. Module boundaries and state

- CubeMX-generated GPIO definitions configure PC13 and PC14 as low-speed push-pull outputs without pulls and write both pins low before configuration. The `.ioc`, `main.h`, and `gpio.c` definitions must agree so regeneration preserves the mapping.
- `app_at_protocol` adds only parsing and command data for the two command families.
- A small pure policy module expresses whether motor and charge bypass activation is allowed. It has no HAL or RTOS dependency and is covered by native unit tests.
- `app_motor_task` owns PC13 writes and all automatic motor-bypass resets.
- `app_output_task` owns PC14 writes and all charge-phase/thermal resets.
- Shared application state records the applied bypass states and the actual charge-output phase needed for command-time validation. State is updated only after the owner writes the GPIO.
- `app_at_task` returns `ERROR:STATE` for a disallowed activation and the existing queue-specific error if a valid request cannot be queued.

No task other than the designated owner writes either new pin after initialization.

## 6. Git delivery

Firmware history is handled as follows:

1. Keep `codex/transparent-mode` unchanged.
2. Develop on `codex/power-resistor-bypass`, created from `4b5eb84` (`merge: set all UART baud rates to 9600`).
3. After verification, move `master` back to `4b5eb84` and merge the feature branch with a merge commit.
4. Preserve and exclude the two existing local `.settings` changes from all commits.

Host history is handled as follows:

1. Preserve the current exclusive transparent-mode master at `codex/transparent-mode`.
2. Restore `master` to `ac74860` (`test: set host baud rate to 9600`).
3. Run the host test suite at that restored commit. No new bypass UI is included in this scope; commands remain available through the existing AT console.

Neither repository has a configured remote, so these local reference changes do not rewrite remote history.

## 7. Error handling and safety

- Both pins default low at reset and before GPIO mode setup to avoid a startup pulse.
- State is checked both before queueing and in the owner task; owner-task validation is authoritative.
- If shared state cannot be read, activation fails conservatively with the existing state-busy behavior and the output stays low.
- A queue failure cannot change the GPIO and returns an explicit queue error.
- Stop and protection paths write the bypass low before disabling or changing the associated power path.
- Hardware tests do not intentionally stall the motor or defeat thermal protection.

## 8. Verification strategy

### 8.1 Automated verification

Test-first coverage will prove:

- strict parsing of all four valid forms and rejection of wrong case, spacing, suffixes, and invalid values;
- motor activation allowed only for `FWD` and `REV`;
- charge activation allowed only during the actual charge ON phase;
- OFF always allowed;
- PC13 resets on stop, sleep, brake, stall, and direction change;
- PC14 resets on the cycle OFF boundary, power-mode changes, and thermal stop;
- GPIO initialization and CubeMX metadata define PC13/PC14 as push-pull, no-pull, low-speed outputs with low defaults;
- all existing native C and Python contract tests remain green;
- Debug and Release firmware builds succeed;
- the restored host test suite succeeds at 9600 baud.

### 8.2 Hardware verification

Use the connected ST-Link to program and verify the built firmware, then use the connected FTDI adapter on COM12 at 9600 8N1 over UART1 RS-485.

The hardware record must capture exact commands and responses for:

1. `AT+VERSION?` handshake after reset.
2. Both bypass activations rejected with `ERROR:STATE` while their loads are inactive.
3. Motor `FWD` and `REV` startup with PC13 low, successful activation after startup, automatic low on stop and direction change, and continued AT traffic while the motor runs.
4. Charge ON phase with PC14 initially low, successful activation during that phase, and automatic low at the OFF boundary or explicit stop.
5. Thermal and stall reset behavior through safe observable paths where the connected setup permits it; any unsafe-to-induce protection path remains supported by automated evidence and is recorded honestly rather than claimed as physically induced.

ST-Link GPIO register reads may supplement AT responses to prove GPIOC ODR bits 13 and 14. If live register reads are not reliable while the target runs, the record must distinguish protocol/state verification from direct electrical-level verification.

## 9. Documentation delivery

Update `README.md`, `上位机AT命令文档.md`, and `钻杆mcu控制功能.md` with the new pin meanings, command grammar, state errors, automatic-reset rules, restored 9600-baud topology, and simultaneous motor/AT communication behavior. Add a dated hardware test record under `docs/` containing firmware commit, build artifact, ST-Link identity, COM port, commands, responses, register observations, and limitations.

After the feature merge, rerun the relevant automated suites and verify the final `master` references in both repositories.
