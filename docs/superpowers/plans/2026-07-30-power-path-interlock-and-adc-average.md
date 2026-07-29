# Power Path Interlock and ADC Rolling Average Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enforce a three-state LM51770/MP4317 power path in firmware, migrate the host to five safe AT commands, smooth five sensor channels over five cycles without consuming task stack, and verify both products on real hardware.

**Architecture:** The AT parser emits one `AppPowerMode`; one output-queue item owns the complete safe-off-before-enable sequence. A small platform-independent sequencer makes GPIO order natively testable. Five static rolling filters maintain raw ADC rings and running sums. The host sends one atomic power command per mode and mirrors the same invariant in fake mode and UI terminology.

**Tech Stack:** STM32F103/HAL/FreeRTOS/C11, MinGW native C tests, CMake/Ninja/GNU Arm Embedded, STM32CubeProgrammer/ST-Link, UART1 COM12, Python 3.11/PySide6/pytest/PyInstaller.

---

## File Map

### Firmware repository: `D:\Codes\STM32\stem-hub`

- Create `App/Inc/app_power_path.h` and `App/Src/app_power_path.c`: platform-independent safe transition ordering.
- Create `App/Inc/app_adc_filter.h` and `App/Src/app_adc_filter.c`: fixed-size five-sample rolling mean.
- Create `tests/test_power_path.c` and `tests/test_adc_filter.c`: native unit tests.
- Modify `App/Inc/app_at_protocol.h`, `App/Src/app_at_protocol.c`, and `tests/test_at_protocol.c`: five-command contract.
- Modify `App/Inc/app_types.h`, `App/Inc/app_output.h`, `App/Src/app_output_task.c`, `App/Src/app_at_task.c`, and `App/Src/app_runtime.c`: atomic queued power-mode request.
- Modify `App/Src/app_sensor_task.c`: static per-channel filters.
- Modify `App/Inc/app_config.h` and `cmake/stm32cubemx/CMakeLists.txt`: version and sources.
- Modify `README.md`, `上位机AT命令文档.md`, and `钻杆mcu控制功能.md`: current protocol and filtering behavior.

### Host repository: `D:\Codes\STM32\stem-hub-host`

- Modify `stem_hub_host/at_protocol.py` and `tests/test_at_protocol.py`: new builders and removal of chip builders.
- Modify `stem_hub_host/controller.py` and `tests/test_behavior_regressions.py`: one-command transitions and safe ALL OFF.
- Modify `stem_hub_host/fake_firmware.py` and `tests/test_connection_flow.py`: strict fake power mode.
- Modify `stem_hub_host/ui/main_window.py`, `stem_hub_host/ui/widgets/charge_mode_card.py`, and UI tests: rename DISCHARGE to DRIVE.
- Modify `README.md` and create `docs/power-path-at-contract.md`: current command vocabulary and host/firmware mapping.

## Task 1: Lock the firmware AT contract

**Files:**
- Modify: `D:\Codes\STM32\stem-hub\tests\test_at_protocol.c`
- Modify: `D:\Codes\STM32\stem-hub\App\Inc\app_at_protocol.h`
- Modify: `D:\Codes\STM32\stem-hub\App\Src\app_at_protocol.c`

- [ ] **Step 1: Add failing parser assertions**

Add a helper that asserts the parsed power mode, then cover all five accepted
commands and removed forms:

```c
static void expect_power_command(const char *line, AppPowerMode expected_mode)
{
    AppAtCommand command = {0};
    assert(AppAtProtocol_Parse(line, &command));
    assert(command.type == APP_AT_COMMAND_SET_POWER_MODE);
    assert(command.data.power.mode == expected_mode);
}

expect_power_command("AT+CHARGE=ON\r\n", APP_POWER_MODE_CHARGE);
expect_power_command("AT+CHARGE=OFF\r\n", APP_POWER_MODE_OFF);
expect_power_command("AT+DRIVE=ON\r\n", APP_POWER_MODE_DRIVE);
expect_power_command("AT+DRIVE=OFF\r\n", APP_POWER_MODE_OFF);
expect_power_command("AT+POWER=OFF\r\n", APP_POWER_MODE_OFF);
assert(!AppAtProtocol_Parse("AT+LM51770=ON\r\n", &command));
assert(!AppAtProtocol_Parse("AT+LM51770=OFF\r\n", &command));
assert(!AppAtProtocol_Parse("AT+MP4317=ON\r\n", &command));
assert(!AppAtProtocol_Parse("AT+MP4317=OFF\r\n", &command));
assert(!AppAtProtocol_Parse("AT+POWER=ON\r\n", &command));
```

- [ ] **Step 2: Compile and run the parser test to prove RED**

```powershell
New-Item -ItemType Directory -Force 'D:\Codes\STM32\stem-hub-host\build\contract-tests' | Out-Null
& 'D:\Toolchains\MinGW\bin\gcc.exe' -std=c11 -Wall -Wextra -Werror `
  -I 'D:\Codes\STM32\stem-hub\App\Inc' `
  'D:\Codes\STM32\stem-hub\tests\test_at_protocol.c' `
  'D:\Codes\STM32\stem-hub\App\Src\app_at_protocol.c' `
  -o 'D:\Codes\STM32\stem-hub-host\build\contract-tests\test_at_protocol.exe'
```

Expected: compile fails because `AppPowerMode` and
`APP_AT_COMMAND_SET_POWER_MODE` do not exist.

- [ ] **Step 3: Implement the minimal parser contract**

Define:

```c
typedef enum
{
    APP_POWER_MODE_OFF = 0,
    APP_POWER_MODE_CHARGE,
    APP_POWER_MODE_DRIVE
} AppPowerMode;
```

Replace the two chip command types with `APP_AT_COMMAND_SET_POWER_MODE`, add
`data.power.mode`, parse `CHARGE` and `DRIVE` through `ParseOnOff`, and accept
only the literal `AT+POWER=OFF`. Remove both old assignment branches.

- [ ] **Step 4: Recompile and run to prove GREEN**

Run the Step 2 compiler command and:

```powershell
& 'D:\Codes\STM32\stem-hub-host\build\contract-tests\test_at_protocol.exe'
```

Expected: exit code 0 with no output.

- [ ] **Step 5: Commit the parser contract**

```powershell
git -C 'D:\Codes\STM32\stem-hub' add App/Inc/app_at_protocol.h App/Src/app_at_protocol.c tests/test_at_protocol.c
git -C 'D:\Codes\STM32\stem-hub' commit -m 'feat(at): replace chip controls with power modes'
```

## Task 2: Make the safe transition order executable and testable

**Files:**
- Create: `D:\Codes\STM32\stem-hub\App\Inc\app_power_path.h`
- Create: `D:\Codes\STM32\stem-hub\App\Src\app_power_path.c`
- Create: `D:\Codes\STM32\stem-hub\tests\test_power_path.c`
- Modify: `D:\Codes\STM32\stem-hub\App\Inc\app_types.h`
- Modify: `D:\Codes\STM32\stem-hub\App\Inc\app_output.h`
- Modify: `D:\Codes\STM32\stem-hub\App\Src\app_output_task.c`
- Modify: `D:\Codes\STM32\stem-hub\App\Src\app_at_task.c`
- Modify: `D:\Codes\STM32\stem-hub\App\Src\app_runtime.c`
- Modify: `D:\Codes\STM32\stem-hub\cmake\stm32cubemx\CMakeLists.txt`

- [ ] **Step 1: Write the failing transition-order test**

Record callback events as `(target, enabled)` and assert:

```c
assert(App_PowerPathApply(APP_POWER_MODE_OFF, RecordWrite, &log));
assert_event(&log, 0U, APP_OUTPUT_TARGET_UVLO, false);
assert_event(&log, 1U, APP_OUTPUT_TARGET_MP4317, false);

reset_log(&log);
assert(App_PowerPathApply(APP_POWER_MODE_CHARGE, RecordWrite, &log));
assert_event(&log, 0U, APP_OUTPUT_TARGET_UVLO, false);
assert_event(&log, 1U, APP_OUTPUT_TARGET_MP4317, false);
assert_event(&log, 2U, APP_OUTPUT_TARGET_UVLO, true);

reset_log(&log);
assert(App_PowerPathApply(APP_POWER_MODE_DRIVE, RecordWrite, &log));
assert_event(&log, 0U, APP_OUTPUT_TARGET_UVLO, false);
assert_event(&log, 1U, APP_OUTPUT_TARGET_MP4317, false);
assert_event(&log, 2U, APP_OUTPUT_TARGET_MP4317, true);
```

- [ ] **Step 2: Compile the missing module to prove RED**

```powershell
& 'D:\Toolchains\MinGW\bin\gcc.exe' -std=c11 -Wall -Wextra -Werror `
  -I 'D:\Codes\STM32\stem-hub\App\Inc' `
  'D:\Codes\STM32\stem-hub\tests\test_power_path.c' `
  'D:\Codes\STM32\stem-hub\App\Src\app_power_path.c' `
  -o 'D:\Codes\STM32\stem-hub-host\build\contract-tests\test_power_path.exe'
```

Expected: compile fails because the new files/API do not exist.

- [ ] **Step 3: Implement the sequencer and one queue-item request**

Expose:

```c
typedef void (*AppPowerPathWrite)(AppOutputTarget target, bool enabled, void *context);
bool App_PowerPathApply(AppPowerMode mode, AppPowerPathWrite write, void *context);
bool App_OutputEnqueuePowerMode(AppPowerMode mode);
```

`App_PowerPathApply()` calls the callback for UVLO-off, MP4317-off, then
exactly one enable callback for Charge or Drive. Extend `AppOutputRequest`
with a request kind and power mode. The output task invokes the sequencer
using a callback that performs the existing active-low HAL write and
`App_StateSetOutputEnabled()`. `app_at_task.c` queues one mode request and
returns `OUTPUT_QUEUE` on failure.

- [ ] **Step 4: Run native and firmware builds**

Run the Step 2 compiler command, execute the resulting test, then configure
and build Debug with the bundled tools:

```powershell
$env:Path = 'C:\Users\44575\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\13.3.1+st.9\bin;' + $env:Path
& 'C:\Users\44575\AppData\Local\stm32cube\bundles\cmake\4.0.1+st.3\bin\cmake.exe' --preset Debug
& 'C:\Users\44575\AppData\Local\stm32cube\bundles\cmake\4.0.1+st.3\bin\cmake.exe' --build --preset Debug
```

Expected: native test exits 0 and `stem-hub.elf` links without warnings or
errors.

- [ ] **Step 5: Commit atomic power-path execution**

```powershell
git -C 'D:\Codes\STM32\stem-hub' add App/Inc/app_power_path.h App/Src/app_power_path.c tests/test_power_path.c App/Inc/app_types.h App/Inc/app_output.h App/Src/app_output_task.c App/Src/app_at_task.c App/Src/app_runtime.c cmake/stm32cubemx/CMakeLists.txt
git -C 'D:\Codes\STM32\stem-hub' commit -m 'feat(power): enforce atomic charge drive interlock'
```

## Task 3: Add the five-cycle rolling ADC mean

**Files:**
- Create: `D:\Codes\STM32\stem-hub\App\Inc\app_adc_filter.h`
- Create: `D:\Codes\STM32\stem-hub\App\Src\app_adc_filter.c`
- Create: `D:\Codes\STM32\stem-hub\tests\test_adc_filter.c`
- Modify: `D:\Codes\STM32\stem-hub\App\Src\app_sensor_task.c`
- Modify: `D:\Codes\STM32\stem-hub\cmake\stm32cubemx\CMakeLists.txt`

- [ ] **Step 1: Write rolling-mean tests**

Exercise startup, full-window replacement, and maximum values:

```c
AppAdcRollingMean filter = {0};
assert(App_AdcRollingMeanPush(&filter, 100U) == 100U);
assert(App_AdcRollingMeanPush(&filter, 200U) == 150U);
assert(App_AdcRollingMeanPush(&filter, 300U) == 200U);
assert(App_AdcRollingMeanPush(&filter, 400U) == 250U);
assert(App_AdcRollingMeanPush(&filter, 500U) == 300U);
assert(App_AdcRollingMeanPush(&filter, 600U) == 400U);

AppAdcRollingMean maximum = {0};
for (size_t index = 0U; index < APP_ADC_ROLLING_WINDOW_SIZE; ++index)
{
    assert(App_AdcRollingMeanPush(&maximum, 4095U) == 4095U);
}
assert(maximum.sum == 20475U);
```

- [ ] **Step 2: Compile to prove RED**

```powershell
& 'D:\Toolchains\MinGW\bin\gcc.exe' -std=c11 -Wall -Wextra -Werror `
  -I 'D:\Codes\STM32\stem-hub\App\Inc' `
  'D:\Codes\STM32\stem-hub\tests\test_adc_filter.c' `
  'D:\Codes\STM32\stem-hub\App\Src\app_adc_filter.c' `
  -o 'D:\Codes\STM32\stem-hub-host\build\contract-tests\test_adc_filter.exe'
```

Expected: compile fails because the rolling filter does not exist.

- [ ] **Step 3: Implement fixed static filters**

Define a five-element `uint16_t samples[]`, `uint32_t sum`, `uint8_t
next_index`, and `uint8_t count`. `Push` subtracts the outgoing element only
when full, adds the new sample, advances modulo five, and returns
`sum / count`.

In `app_sensor_task.c`, allocate exactly five file-scope filter objects and
push a raw sample only after its ADC read succeeds:

```c
static AppAdcRollingMean g_sensor_filters[5];
filtered_raw = App_AdcRollingMeanPush(&g_sensor_filters[channel_index], raw);
App_SensorUpdateMeasure(measure, filtered_raw, convert_fn);
```

Do not alter the motor-current acquisition or protection path.

- [ ] **Step 4: Run the filter test and Debug build**

Run the Step 2 compiler command and executable, then the Debug build commands
from Task 2. Expected: all exit 0 and the linker memory report remains within
STM32F103C8 flash/RAM limits.

- [ ] **Step 5: Commit ADC filtering**

```powershell
git -C 'D:\Codes\STM32\stem-hub' add App/Inc/app_adc_filter.h App/Src/app_adc_filter.c tests/test_adc_filter.c App/Src/app_sensor_task.c cmake/stm32cubemx/CMakeLists.txt
git -C 'D:\Codes\STM32\stem-hub' commit -m 'feat(sensor): average five sampling cycles'
```

## Task 4: Migrate the host protocol and controller

**Files:**
- Modify: `D:\Codes\STM32\stem-hub-host\tests\test_at_protocol.py`
- Modify: `D:\Codes\STM32\stem-hub-host\tests\test_behavior_regressions.py`
- Modify: `D:\Codes\STM32\stem-hub-host\stem_hub_host\at_protocol.py`
- Modify: `D:\Codes\STM32\stem-hub-host\stem_hub_host\controller.py`

- [ ] **Step 1: Replace protocol expectations with the five commands**

```python
assert cmd_set_charge(True) == "AT+CHARGE=ON\r\n"
assert cmd_set_charge(False) == "AT+CHARGE=OFF\r\n"
assert cmd_set_drive(True) == "AT+DRIVE=ON\r\n"
assert cmd_set_drive(False) == "AT+DRIVE=OFF\r\n"
assert cmd_power_off() == "AT+POWER=OFF\r\n"
```

Update controller tests so Charge, Drive, Off each write one exact command;
ALL OFF must begin with `AT+POWER=OFF\r\n` followed by NMOS1, NMOS2, and LED
off.

- [ ] **Step 2: Run focused pytest to prove RED**

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest `
  tests/test_at_protocol.py tests/test_behavior_regressions.py -q
```

Expected: import/assertion failures for missing new builders and old command
sequences.

- [ ] **Step 3: Implement builders and single-command transitions**

```python
def cmd_set_charge(on: bool) -> str:
    return f"AT+CHARGE={'ON' if on else 'OFF'}{CRLF}"

def cmd_set_drive(on: bool) -> str:
    return f"AT+DRIVE={'ON' if on else 'OFF'}{CRLF}"

def cmd_power_off() -> str:
    return f"AT+POWER=OFF{CRLF}"
```

Remove `cmd_set_lm51770()` and `cmd_set_mp4317()`. Map controller modes
`charge`, `drive`, and `off` to one command each while retaining existing
FIFO acknowledgement and rapid-toggle serialization. Start global ALL OFF
with `cmd_power_off()`.

- [ ] **Step 4: Run focused tests to prove GREEN**

Run Step 2. Expected: all selected tests pass.

- [ ] **Step 5: Commit host protocol/controller migration**

```powershell
git -C 'D:\Codes\STM32\stem-hub-host' add stem_hub_host/at_protocol.py stem_hub_host/controller.py tests/test_at_protocol.py tests/test_behavior_regressions.py
git -C 'D:\Codes\STM32\stem-hub-host' commit -m 'feat(power): use atomic charge drive commands'
```

## Task 5: Synchronize fake firmware and UI semantics

**Files:**
- Modify: `D:\Codes\STM32\stem-hub-host\stem_hub_host\fake_firmware.py`
- Modify: `D:\Codes\STM32\stem-hub-host\stem_hub_host\ui\main_window.py`
- Modify: `D:\Codes\STM32\stem-hub-host\stem_hub_host\ui\widgets\charge_mode_card.py`
- Modify: `D:\Codes\STM32\stem-hub-host\tests\test_connection_flow.py`
- Modify: `D:\Codes\STM32\stem-hub-host\tests\test_output_controls.py`
- Modify: `D:\Codes\STM32\stem-hub-host\tests\test_main_window.py`

- [ ] **Step 1: Write failing fake/UI behavior tests**

Assert fake commands always clear both flags before selecting one:

```python
send("AT+CHARGE=ON\r\n")
assert fake._lm51770 is True
assert fake._mp4317 is False
send("AT+DRIVE=ON\r\n")
assert fake._lm51770 is False
assert fake._mp4317 is True
send("AT+POWER=OFF\r\n")
assert fake._lm51770 is False
assert fake._mp4317 is False
```

Assert the charge card exposes `CHARGE` and `DRIVE`, not `DISCHARGE`, and
that toggling either calls `set_charge_mode("charge")` or
`set_charge_mode("drive")`.

- [ ] **Step 2: Run focused tests to prove RED**

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest `
  tests/test_connection_flow.py tests/test_output_controls.py tests/test_main_window.py -q
```

Expected: failures referencing old fake commands and DISCHARGE label.

- [ ] **Step 3: Implement fake and UI migration**

Add `_set_power_mode(mode)` that sets both booleans false before enabling the
selected device. Handle only `AT+CHARGE=ON/OFF`, `AT+DRIVE=ON/OFF`, and
`AT+POWER=OFF`; old commands fall through to the normal error response.
Change `CHARGE_TOGGLE_MAP` and card labels from `DISCHARGE` to `DRIVE`.

- [ ] **Step 4: Run focused and complete host tests**

Run Step 2, then:

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest tests -q
```

Expected: complete suite passes with zero failures.

- [ ] **Step 5: Commit fake/UI migration**

```powershell
git -C 'D:\Codes\STM32\stem-hub-host' add stem_hub_host/fake_firmware.py stem_hub_host/ui/main_window.py stem_hub_host/ui/widgets/charge_mode_card.py tests/test_connection_flow.py tests/test_output_controls.py tests/test_main_window.py
git -C 'D:\Codes\STM32\stem-hub-host' commit -m 'feat(ui): expose charge and drive power modes'
```

## Task 6: Update both repositories' documentation

**Files:**
- Modify: `D:\Codes\STM32\stem-hub\README.md`
- Modify: `D:\Codes\STM32\stem-hub\上位机AT命令文档.md`
- Modify: `D:\Codes\STM32\stem-hub\钻杆mcu控制功能.md`
- Modify: `D:\Codes\STM32\stem-hub\App\Inc\app_config.h`
- Modify: `D:\Codes\STM32\stem-hub-host\README.md`
- Create: `D:\Codes\STM32\stem-hub-host\docs\power-path-at-contract.md`

- [ ] **Step 1: Scan tracked current documentation for stale commands**

```powershell
rg -n 'AT\+(LM51770|MP4317)|DISCHARGE|最近一次传感采样' `
  'D:\Codes\STM32\stem-hub' 'D:\Codes\STM32\stem-hub-host' `
  -g '*.md' --glob '!docs/superpowers/plans/**' --glob '!docs/superpowers/reports/**'
```

The current documents modified by this task are the two READMEs, the two
Chinese MCU guides, and the new host contract document. Historical
superpowers specifications, plans, and reports remain unchanged.

- [ ] **Step 2: Update current firmware and host documentation**

Document the five exact commands, active-low safe sequence, three states,
removed commands, five-cycle startup/full-window behavior, static RAM rather
than task-stack storage, motor-current safety exception, COM12 verification,
and host `DRIVE` label. Create `docs/power-path-at-contract.md` in the host
repository as its current protocol reference. Advance `APP_FIRMWARE_VERSION`
from `release-v2.2` to `release-v3.0` because the AT contract is breaking.

- [ ] **Step 3: Prove current docs contain no stale contract**

Run Step 1 again. Expected: no matches outside explicitly historical
spec/plan/report documents. Run `git diff --check` in both repositories.

- [ ] **Step 4: Commit documentation in each repository**

```powershell
git -C 'D:\Codes\STM32\stem-hub' add README.md 上位机AT命令文档.md 钻杆mcu控制功能.md App/Inc/app_config.h
git -C 'D:\Codes\STM32\stem-hub' commit -m 'docs(v3): document interlocked power modes'
git -C 'D:\Codes\STM32\stem-hub-host' add README.md docs/power-path-at-contract.md
git -C 'D:\Codes\STM32\stem-hub-host' commit -m 'docs: sync atomic power path contract'
```

## Task 7: Full build, flash, and hardware verification

**Files:**
- Generate ignored artifacts under firmware `build/Debug`
- Generate ignored host artifacts under `build/` and `dist/`

- [ ] **Step 1: Run all native firmware tests and Debug build**

Compile and run every native test with MinGW:

```powershell
$gcc = 'D:\Toolchains\MinGW\bin\gcc.exe'
$repo = 'D:\Codes\STM32\stem-hub'
$out = 'D:\Codes\STM32\stem-hub-host\build\contract-tests'
New-Item -ItemType Directory -Force $out | Out-Null
& $gcc -std=c11 -Wall -Wextra -Werror -I "$repo\App\Inc" "$repo\tests\test_at_protocol.c" "$repo\App\Src\app_at_protocol.c" -o "$out\test_at_protocol.exe"
& $gcc -std=c11 -Wall -Wextra -Werror -I "$repo\App\Inc" "$repo\tests\test_power_path.c" "$repo\App\Src\app_power_path.c" -o "$out\test_power_path.exe"
& $gcc -std=c11 -Wall -Wextra -Werror -I "$repo\App\Inc" "$repo\tests\test_adc_filter.c" "$repo\App\Src\app_adc_filter.c" -o "$out\test_adc_filter.exe"
& $gcc -std=c11 -Wall -Wextra -Werror -I "$repo\App\Inc" "$repo\tests\test_uart_tunnel.c" "$repo\App\Src\app_uart_tunnel.c" -o "$out\test_uart_tunnel.exe"
foreach ($name in @('test_batt_voltage_conversion','test_batt_ntc_temperature','test_ntc_temperature','test_motor_current_conversion')) {
    & $gcc -std=c11 -Wall -Wextra -Werror "$repo\tests\$name.c" -o "$out\$name.exe"
}
Get-ChildItem -LiteralPath $out -Filter 'test_*.exe' | ForEach-Object {
    & $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "$($_.Name) failed" }
}
```

Then re-run the bundled CMake Debug build commands from Task 2. Expected:
every executable exits 0 and the ELF/HEX/BIN are regenerated.

- [ ] **Step 2: Rebuild and smoke-test the host executable**

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m compileall -q stem_hub_host
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest tests -q
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m PyInstaller --clean --noconfirm stem-hub-host.spec
```

Launch `dist\stem-hub-host.exe --fake`, require it to remain running for at
least five seconds, then close only that launched process. Expected:
executable exists, starts, and does not exit unexpectedly.

- [ ] **Step 3: Program the MCU through connected ST-Link**

Use the newest bundled CubeProgrammer:

```powershell
& 'C:\Users\44575\AppData\Local\stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe' `
  -c port=SWD mode=UR reset=HWrst `
  -d 'D:\Codes\STM32\stem-hub\build\Debug\stem-hub.elf' `
  -v -rst
```

Expected: download, verification, and reset report success.

- [ ] **Step 4: Verify the real AT contract on COM12**

At 115200 8N1, send `AT+VERSION?`, the five new commands, the four removed
chip commands, `AT+POWER=ON`, and at least seven `AT+SENSE?` queries spaced
one second apart. Require:

- version reports `release-v3.0`;
- every new command returns `OK`;
- every removed/invalid command returns `ERROR:BAD_COMMAND`;
- SENSE count advances;
- after the fifth sample, `STK_SENSOR` is nonzero and remains stable;
- no `+FAIL:` frame, timeout, or UART stall occurs.

- [ ] **Step 5: Verify real GPIO states through ST-Link if non-disruptive**

After each COM12 command, read GPIOA ODR at `0x4001080C` and GPIOB ODR at
`0x40010C0C`. Active-low expectations:

- Off: PA8=1 and PB3=1;
- Charge: PA8=1 and PB3=0;
- Drive: PA8=0 and PB3=1.

If CubeProgrammer memory reads reset or halt UART execution, use a GDB-server
attach without reset and resume immediately; do not claim GPIO verification
unless actual register values are captured.

## Task 8: Completion audit and merge to both master branches

**Files:**
- No source changes expected

- [ ] **Step 1: Audit requirements and repository state**

Confirm tests/build/hardware evidence covers all requirements. Run:

```powershell
git -C 'D:\Codes\STM32\stem-hub' status --short --branch
git -C 'D:\Codes\STM32\stem-hub-host' status --short --branch
rg -n 'AT\+(LM51770|MP4317)' 'D:\Codes\STM32\stem-hub\App' 'D:\Codes\STM32\stem-hub-host\stem_hub_host' 'D:\Codes\STM32\stem-hub-host\tests'
```

Expected: only the two pre-existing MCU `.settings` line-ending entries are
dirty; source/test scan has no old commands.

- [ ] **Step 2: Merge MCU feature branch into MCU master**

```powershell
git -C 'D:\Codes\STM32\stem-hub' switch master
git -C 'D:\Codes\STM32\stem-hub' merge --no-ff codex/power-path-interlock -m 'merge: power path interlock and adc averaging'
```

The pre-existing `.settings` modifications remain unstaged and uncommitted.

- [ ] **Step 3: Merge host feature branch into host master**

```powershell
git -C 'D:\Codes\STM32\stem-hub-host' switch master
git -C 'D:\Codes\STM32\stem-hub-host' merge --no-ff codex/power-path-interlock -m 'merge: power path interlock support'
```

- [ ] **Step 4: Verify merge tips and deliverables**

Check both logs contain merge commits, host `dist\stem-hub-host.exe` has the
current build timestamp, firmware `build\Debug\stem-hub.elf` matches the
flashed image, and both repositories are on `master`.
