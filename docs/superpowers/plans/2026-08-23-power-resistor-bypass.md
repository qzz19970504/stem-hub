# Power Resistor Bypass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the 9600-baud simultaneous AT/RS-485 baseline, add safety-interlocked PC13 motor-resistor and PC14 charge-resistor bypass commands, verify them on the connected STM32 hardware, restore the host baseline, and merge the tested firmware feature.

**Architecture:** A HAL-free policy module defines whether a requested bypass activation is safe. The AT task performs an early state check and queues valid requests; the motor task remains the sole PC13 writer and the output task remains the sole PC14 writer, each revalidating and forcing its pin low on every associated safety transition. GPIO metadata, generated initialization, shared applied state, protocol documentation, and reproducible hardware evidence change together.

**Tech Stack:** C11, STM32F103 HAL, CMSIS-RTOS2/FreeRTOS, CMake/Ninja/arm-none-eabi-gcc, MinGW GCC native tests, Python/pytest contract tests, STM32CubeProgrammer, ST-Link, pyserial, UART1 RS-485 at 9600 8N1, Python/PySide6 host application.

---

## File map

- Create `App/Inc/app_resistor_bypass.h` and `App/Src/app_resistor_bypass.c`: pure activation policy with no HAL or RTOS includes.
- Create `tests/test_resistor_bypass.c`: native policy tests.
- Modify `App/Inc/app_at_protocol.h`, `App/Src/app_at_protocol.c`, and `tests/test_at_protocol.c`: parse the two additive AT command families.
- Modify `App/Inc/app_types.h`, `App/Inc/app_state.h`, `App/Src/app_state.c`, and `tests/test_app_state.c`: typed motor/output requests and applied bypass state.
- Modify `App/Inc/app_motor.h` and `App/Src/app_motor_task.c`: queue and own PC13, including stop/stall/direction resets.
- Modify `App/Inc/app_output.h` and `App/Src/app_output_task.c`: queue and own PC14, including charge-phase and thermal resets.
- Modify `App/Src/app_at_task.c`: command-time state validation, `ERROR:STATE`, and queue errors.
- Modify `App/Src/app_runtime.c`: initialize the expanded request objects and safe GPIO state.
- Modify `Core/Inc/main.h`, `Core/Src/gpio.c`, and `stem-hub.ioc`: durable PC13/PC14 push-pull mappings with low defaults.
- Modify `cmake/stm32cubemx/CMakeLists.txt`: compile the policy source into firmware.
- Create `tests/test_resistor_bypass_contract.py`: task ownership, reset-path, AT error, GPIO, and CubeMX contract checks.
- Create `tools/resistor_bypass_hardware_test.py`: repeatable COM12 AT sequence with safe cleanup.
- Modify `README.md`, `上位机AT命令文档.md`, and `钻杆mcu控制功能.md`: user-facing behavior and restored topology.
- Create `docs/power-resistor-bypass-hardware-test-2026-08-23.md`: exact hardware evidence and limitations.
- In `D:/Codes/STM32/stem-hub-host`, preserve `codex/transparent-mode` and restore `master` to `ac74860`; no host source changes are planned.

### Task 1: Add strict protocol types and pure bypass policy

**Files:**
- Create: `App/Inc/app_resistor_bypass.h`
- Create: `App/Src/app_resistor_bypass.c`
- Create: `tests/test_resistor_bypass.c`
- Modify: `App/Inc/app_at_protocol.h`
- Modify: `App/Src/app_at_protocol.c`
- Modify: `tests/test_at_protocol.c`
- Modify: `cmake/stm32cubemx/CMakeLists.txt`

- [ ] **Step 1: Write failing policy and parser tests**

Add native assertions equivalent to:

```c
assert(!App_ResistorBypassMotorActivationAllowed(APP_MOTOR_MODE_SLEEP));
assert(App_ResistorBypassMotorActivationAllowed(APP_MOTOR_MODE_FORWARD));
assert(App_ResistorBypassMotorActivationAllowed(APP_MOTOR_MODE_REVERSE));
assert(!App_ResistorBypassChargeActivationAllowed(false));
assert(App_ResistorBypassChargeActivationAllowed(true));
assert(App_ResistorBypassRequestAllowed(false, false));
```

Add parser coverage using a helper that checks command type and `data.output.enabled`:

```c
expect_boolean_command("AT+MOTOR_BYPASS=ON\r\n",
                       APP_AT_COMMAND_SET_MOTOR_BYPASS,
                       true);
expect_boolean_command("AT+CHARGE_BYPASS=OFF\r\n",
                       APP_AT_COMMAND_SET_CHARGE_BYPASS,
                       false);
assert(!AppAtProtocol_Parse("AT+MOTOR_BYPASS=HIGH\r\n", &command));
assert(!AppAtProtocol_Parse("AT+charge_bypass=ON\r\n", &command));
```

- [ ] **Step 2: Run the tests and verify RED**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_resistor_bypass.c App/Src/app_resistor_bypass.c -o "$env:TEMP\test_resistor_bypass.exe"
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_at_protocol.c App/Src/app_at_protocol.c -o "$env:TEMP\test_at_protocol.exe"
```

Expected: compilation fails because the policy files, command enum values, and parsing branches do not yet exist.

- [ ] **Step 3: Implement the minimal policy API and parser branches**

Define the policy interface exactly as:

```c
bool App_ResistorBypassMotorActivationAllowed(AppMotorMode mode);
bool App_ResistorBypassChargeActivationAllowed(bool charge_output_enabled);
bool App_ResistorBypassRequestAllowed(bool requested_enabled,
                                      bool activation_allowed);
```

The implementations return true only for FWD/REV, actual charge output enabled, or any OFF request respectively. Add `APP_AT_COMMAND_SET_MOTOR_BYPASS` and `APP_AT_COMMAND_SET_CHARGE_BYPASS`; reuse `AppAtBooleanCommand output` for the payload. Parse only `ON` and `OFF` through the existing strict helper. Add `app_resistor_bypass.c` to the firmware source list.

- [ ] **Step 4: Run the focused tests and verify GREEN**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_resistor_bypass.c App/Src/app_resistor_bypass.c -o "$env:TEMP\test_resistor_bypass.exe"
& "$env:TEMP\test_resistor_bypass.exe"
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_at_protocol.c App/Src/app_at_protocol.c -o "$env:TEMP\test_at_protocol.exe"
& "$env:TEMP\test_at_protocol.exe"
```

Expected: both executables exit 0 with no assertion output.

- [ ] **Step 5: Commit the protocol and policy**

```powershell
git add -- App/Inc/app_resistor_bypass.h App/Src/app_resistor_bypass.c tests/test_resistor_bypass.c App/Inc/app_at_protocol.h App/Src/app_at_protocol.c tests/test_at_protocol.c cmake/stm32cubemx/CMakeLists.txt
git commit -m "feat: define resistor bypass commands and policy"
```

### Task 2: Add applied state and typed owner requests

**Files:**
- Modify: `App/Inc/app_types.h`
- Modify: `App/Inc/app_state.h`
- Modify: `App/Src/app_state.c`
- Modify: `tests/test_app_state.c`

- [ ] **Step 1: Write failing shared-state tests**

Extend `test_app_state.c` to verify defaults and round trips:

```c
AppIoStatus io_status = {0};
assert(App_StateTryGetIoStatus(&io_status));
assert(!io_status.motor_bypass_enabled);
assert(!io_status.charge_bypass_enabled);
App_StateSetMotorBypassEnabled(true);
App_StateSetOutputEnabled(APP_OUTPUT_TARGET_CHARGE_BYPASS, true);
assert(App_StateTryGetIoStatus(&io_status));
assert(io_status.motor_bypass_enabled);
assert(io_status.charge_bypass_enabled);
```

- [ ] **Step 2: Compile and verify RED**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -IApp/Inc tests/test_app_state.c App/Src/app_state.c -o "$env:TEMP\test_app_state.exe"
```

Expected: compilation fails because the new state fields and APIs do not exist.

- [ ] **Step 3: Implement typed requests and applied state**

Replace the mode-only motor request with:

```c
typedef enum {
    APP_MOTOR_REQUEST_SET_MODE = 0,
    APP_MOTOR_REQUEST_SET_BYPASS
} AppMotorRequestType;

typedef struct {
    AppMotorRequestType type;
    union {
        AppMotorMode mode;
        bool bypass_enabled;
    } data;
} AppMotorRequest;
```

Add `APP_OUTPUT_TARGET_CHARGE_BYPASS` and `APP_OUTPUT_REQUEST_SET_CHARGE_BYPASS`. Add `motor_bypass_enabled` and `charge_bypass_enabled` to `AppIoStatus`, initialize both false, implement `App_StateSetMotorBypassEnabled(bool)` and `App_StateTryGetIoStatus(AppIoStatus *)`, and update the output-target switch for the charge field. Keep queue allocation based on `sizeof(AppMotorRequest)` and `sizeof(AppOutputRequest)` so the runtime automatically uses the expanded types.

- [ ] **Step 4: Run state tests and verify GREEN**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -IApp/Inc tests/test_app_state.c App/Src/app_state.c -o "$env:TEMP\test_app_state.exe"
& "$env:TEMP\test_app_state.exe"
```

Expected: exit 0.

- [ ] **Step 5: Commit request and state plumbing**

```powershell
git add -- App/Inc/app_types.h App/Inc/app_state.h App/Src/app_state.c tests/test_app_state.c
git commit -m "feat: track applied resistor bypass state"
```

### Task 3: Configure PC13 and PC14 as safe push-pull outputs

**Files:**
- Modify: `Core/Inc/main.h`
- Modify: `Core/Src/gpio.c`
- Modify: `stem-hub.ioc`
- Modify: `App/Src/app_runtime.c`
- Create: `tests/test_resistor_bypass_contract.py`

- [ ] **Step 1: Write the failing GPIO contract**

Require the exact durable mappings and safety writes:

```python
require("#define MOTOR_BYPASS_Pin GPIO_PIN_13", main_header, "PC13 mapping missing")
require("#define CHARGE_BYPASS_Pin GPIO_PIN_14", main_header, "PC14 mapping missing")
require("MOTOR_BYPASS_Pin|CHARGE_BYPASS_Pin, GPIO_PIN_RESET", gpio_source,
        "both bypass pins must be written low before configuration")
require("GPIO_MODE_OUTPUT_PP", gpio_source, "bypass outputs must be push-pull")
require("PC13-TAMPER-RTC.Signal=GPIO_Output", ioc, "CubeMX PC13 output missing")
require("PC14-OSC32_IN.Signal=GPIO_Output", ioc, "CubeMX PC14 output missing")
require("HAL_GPIO_WritePin(MOTOR_BYPASS_GPIO_Port", runtime_source,
        "runtime safe initialization must clear PC13")
require("HAL_GPIO_WritePin(CHARGE_BYPASS_GPIO_Port", runtime_source,
        "runtime safe initialization must clear PC14")
```

- [ ] **Step 2: Run and verify RED**

```powershell
python -m pytest tests/test_resistor_bypass_contract.py -q
```

Expected: FAIL on missing mappings.

- [ ] **Step 3: Implement the GPIO mapping**

Define `MOTOR_BYPASS` on GPIOC pin 13 and `CHARGE_BYPASS` on GPIOC pin 14. In `MX_GPIO_Init`, write both low before configuring both together as `GPIO_MODE_OUTPUT_PP`, `GPIO_NOPULL`, and `GPIO_SPEED_FREQ_LOW`. Add consistent labeled, locked GPIO outputs and updated pin counts/order to `stem-hub.ioc`. Ensure `SystemClock_Config` does not enable LSE, because PC14 is used as GPIO. Clear both pins again in `App_RuntimeInit` with the other runtime safe outputs.

- [ ] **Step 4: Run the contract and firmware compile**

```powershell
python -m pytest tests/test_resistor_bypass_contract.py -q
cmake --preset Debug
cmake --build --preset Debug
```

Expected: contract PASS and `build/Debug/stem-hub.elf` links successfully.

- [ ] **Step 5: Commit GPIO configuration**

```powershell
git add -- Core/Inc/main.h Core/Src/gpio.c stem-hub.ioc App/Src/app_runtime.c tests/test_resistor_bypass_contract.py
git commit -m "feat: configure resistor bypass GPIO outputs"
```

### Task 4: Make the motor task the sole PC13 owner

**Files:**
- Modify: `App/Inc/app_motor.h`
- Modify: `App/Src/app_motor_task.c`
- Modify: `App/Src/app_at_task.c`
- Modify: `tests/test_resistor_bypass_contract.py`
- Modify: `tests/test_motor_task_contract.py`

- [ ] **Step 1: Add failing motor ownership and reset contracts**

Require:

```python
require("bool App_MotorEnqueueBypass(bool enabled)", motor_source,
        "AT requests need a typed motor-owner queue API")
require("App_MotorSetBypass(false)", motor_source,
        "motor safety transitions must restore startup resistance")
require("APP_MOTOR_REQUEST_SET_BYPASS", motor_source,
        "motor task must consume bypass requests")
require("App_ResistorBypassMotorActivationAllowed", motor_source,
        "owner must revalidate running state")
require("App_MotorSetBypass(false);\n            HAL_GPIO_WritePin(EN_IN1_GPIO_Port",
        motor_source, "stall braking must clear PC13 before EN")
```

Also reject `HAL_GPIO_WritePin(MOTOR_BYPASS_GPIO_Port` in every production source except `app_motor_task.c`, `app_runtime.c`, and generated `gpio.c`.

- [ ] **Step 2: Run and verify RED**

```powershell
python -m pytest tests/test_resistor_bypass_contract.py tests/test_motor_task_contract.py -q
```

Expected: FAIL because the queue API and owner writes are missing.

- [ ] **Step 3: Implement owner control and AT precheck**

Implement `App_MotorSetBypass(bool)` to write PC13 and then update applied state. `App_MotorEnqueueMode` and thermal sleep enqueue typed mode requests. Add `App_MotorEnqueueBypass` for typed bypass requests. In the task, OFF always applies; ON applies only when the current stored mode passes the policy. In `App_MotorApplyMode`, clear PC13 before every non-running transition and before a real direction change; do not clear it for an identical repeated FWD or REV request. Clear PC13 before stall braking.

In the AT handler, for `APP_AT_COMMAND_SET_MOTOR_BYPASS`, OFF queues directly. ON reads motor status, returns `ERROR:STATE_BUSY` if unavailable, returns `ERROR:STATE` unless FWD/REV, and otherwise queues. Queue failure returns `ERROR:MOTOR_QUEUE`.

- [ ] **Step 4: Run focused tests and build**

```powershell
python -m pytest tests/test_resistor_bypass_contract.py tests/test_motor_task_contract.py -q
cmake --build --preset Debug
```

Expected: both contract files PASS and Debug firmware links.

- [ ] **Step 5: Commit PC13 behavior**

```powershell
git add -- App/Inc/app_motor.h App/Src/app_motor_task.c App/Src/app_at_task.c tests/test_resistor_bypass_contract.py tests/test_motor_task_contract.py
git commit -m "feat: interlock motor resistor bypass"
```

### Task 5: Make the output task the sole PC14 owner

**Files:**
- Modify: `App/Inc/app_output.h`
- Modify: `App/Src/app_output_task.c`
- Modify: `App/Src/app_at_task.c`
- Modify: `tests/test_resistor_bypass_contract.py`

- [ ] **Step 1: Add failing charge ownership and reset contracts**

Require:

```python
require("bool App_OutputEnqueueChargeBypass(bool enabled)", output_source,
        "AT requests need a typed output-owner queue API")
require("APP_OUTPUT_REQUEST_SET_CHARGE_BYPASS", output_source,
        "output task must consume charge bypass requests")
require("App_OutputApplyTarget(APP_OUTPUT_TARGET_CHARGE_BYPASS, false)",
        output_source, "every applied power transition must restore precharge")
require("App_ResistorBypassChargeActivationAllowed", output_source,
        "owner must revalidate actual charge output")
```

Reject `HAL_GPIO_WritePin(CHARGE_BYPASS_GPIO_Port` outside `app_output_task.c`, `app_runtime.c`, and generated `gpio.c`.

- [ ] **Step 2: Run and verify RED**

```powershell
python -m pytest tests/test_resistor_bypass_contract.py -q
```

Expected: FAIL because the PC14 owner path is missing.

- [ ] **Step 3: Implement PC14 owner behavior and AT precheck**

Map `APP_OUTPUT_TARGET_CHARGE_BYPASS` to PC14 in `App_OutputApplyTarget`. Add a typed enqueue API. Before every charge-cycle action whose `apply_mode` is true, set PC14 low and then apply OFF, CHARGE, or DRIVE. Thermal stop and explicit power changes already flow through this action path. For a bypass request, OFF always applies; ON reads `AppIoStatus` and applies only when `uvlo_enabled` proves the charge path is in its actual ON phase.

In the AT handler, OFF queues directly. ON reads `AppIoStatus`, returns `ERROR:STATE_BUSY` if unavailable, returns `ERROR:STATE` unless `uvlo_enabled` is true, and otherwise queues. Queue failure returns `ERROR:OUTPUT_QUEUE`.

- [ ] **Step 4: Run focused tests and build**

```powershell
python -m pytest tests/test_resistor_bypass_contract.py -q
gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -IApp/Inc tests/test_app_state.c App/Src/app_state.c -o "$env:TEMP\test_app_state.exe"
& "$env:TEMP\test_app_state.exe"
cmake --build --preset Debug
```

Expected: contract and state tests PASS; Debug firmware links.

- [ ] **Step 5: Commit PC14 behavior**

```powershell
git add -- App/Inc/app_output.h App/Src/app_output_task.c App/Src/app_at_task.c tests/test_resistor_bypass_contract.py
git commit -m "feat: interlock charge resistor bypass"
```

### Task 6: Update documentation and add the hardware runner

**Files:**
- Create: `tools/resistor_bypass_hardware_test.py`
- Modify: `README.md`
- Modify: `上位机AT命令文档.md`
- Modify: `钻杆mcu控制功能.md`

- [ ] **Step 1: Add a hardware runner with conservative cleanup**

The script accepts `--port`, defaults to 9600 baud, sends CRLF-terminated commands, checks expected response tokens, and always executes this cleanup in `finally`:

```python
for command in (
    "AT+MOTOR_BYPASS=OFF",
    "AT+MOTOR=STOP",
    "AT+CHARGE_BYPASS=OFF",
    "AT+CHARGE=OFF",
):
    send_command(serial_port, command)
```

Its default sequence tests only inactive-state rejection and handshake. `--exercise-loads` enables bounded FWD/REV and charge-phase testing; it never induces a stall or thermal fault.

- [ ] **Step 2: Document exact semantics**

Document the four command forms, `OK`, `ERROR:STATE`, and `ERROR:STATE_BUSY`; PC13 direction-change reset; PC14 actual-ON-phase reset; GPIO pin mapping; both low defaults; three UARTs at 9600; and the fact that ordinary AT communication remains available while the motor runs. Remove exclusive-transparent-mode statements because this branch is based on `4b5eb84`.

- [ ] **Step 3: Validate script and docs**

```powershell
python -m py_compile tools/resistor_bypass_hardware_test.py
rg -n "MOTOR_BYPASS|CHARGE_BYPASS|PC13|PC14|9600|ERROR:STATE" README.md 上位机AT命令文档.md 钻杆mcu控制功能.md
git diff --check
```

Expected: Python compile succeeds, all required terms occur in user documentation, and diff check is clean.

- [ ] **Step 4: Commit documentation and runner**

```powershell
git add -- tools/resistor_bypass_hardware_test.py README.md 上位机AT命令文档.md 钻杆mcu控制功能.md
git commit -m "docs: describe resistor bypass controls"
```

### Task 7: Run full automated verification and restore the host master

**Files:**
- No firmware source changes expected.
- Git refs in `D:/Codes/STM32/stem-hub-host` change locally.

- [ ] **Step 1: Run all native C tests**

```powershell
$testOut = Join-Path $env:TEMP 'stem-hub-bypass-tests'
New-Item -ItemType Directory -Force -Path $testOut | Out-Null
$nativeTests = @(
  @{Name='test_at_protocol'; Sources=@('tests/test_at_protocol.c','App/Src/app_at_protocol.c'); Includes=@('App/Inc')},
  @{Name='test_at_command_guard'; Sources=@('tests/test_at_command_guard.c','App/Src/app_at_command_guard.c','App/Src/app_thermal_guard.c'); Includes=@('App/Inc')},
  @{Name='test_app_state'; Sources=@('tests/test_app_state.c','App/Src/app_state.c'); Includes=@('tests/stubs','App/Inc')},
  @{Name='test_adc_filter'; Sources=@('tests/test_adc_filter.c','App/Src/app_adc_filter.c'); Includes=@('App/Inc')},
  @{Name='test_power_path'; Sources=@('tests/test_power_path.c','App/Src/app_power_path.c'); Includes=@('App/Inc')},
  @{Name='test_charge_cycle'; Sources=@('tests/test_charge_cycle.c','App/Src/app_charge_cycle.c'); Includes=@('App/Inc')},
  @{Name='test_thermal_guard'; Sources=@('tests/test_thermal_guard.c','App/Src/app_thermal_guard.c'); Includes=@('App/Inc')},
  @{Name='test_sensor_thermal'; Sources=@('tests/test_sensor_thermal.c','App/Src/app_sensor_thermal.c','App/Src/app_thermal_guard.c'); Includes=@('App/Inc')},
  @{Name='test_task_safety'; Sources=@('tests/test_task_safety.c','App/Src/app_task_safety.c','App/Src/app_thermal_guard.c'); Includes=@('App/Inc')},
  @{Name='test_uart_tunnel'; Sources=@('tests/test_uart_tunnel.c','App/Src/app_uart_tunnel.c'); Includes=@('App/Inc')},
  @{Name='test_batt_voltage_conversion'; Sources=@('tests/test_batt_voltage_conversion.c'); Includes=@('App/Inc')},
  @{Name='test_batt_ntc_temperature'; Sources=@('tests/test_batt_ntc_temperature.c'); Includes=@('App/Inc')},
  @{Name='test_ntc_temperature'; Sources=@('tests/test_ntc_temperature.c'); Includes=@('App/Inc')},
  @{Name='test_motor_current_conversion'; Sources=@('tests/test_motor_current_conversion.c','App/Src/app_motor_current.c'); Includes=@('App/Inc')},
  @{Name='test_motor_stall_guard'; Sources=@('tests/test_motor_stall_guard.c','App/Src/app_motor_stall_guard.c'); Includes=@('App/Inc')},
  @{Name='test_stall_config'; Sources=@('tests/test_stall_config.c','App/Src/app_stall_config.c'); Includes=@('App/Inc')},
  @{Name='test_stall_config_service'; Sources=@('tests/test_stall_config_service.c','App/Src/app_stall_config_service.c'); Includes=@('tests/stubs','App/Inc')},
  @{Name='test_resistor_bypass'; Sources=@('tests/test_resistor_bypass.c','App/Src/app_resistor_bypass.c'); Includes=@('App/Inc')}
)
foreach ($test in $nativeTests) {
  $includeArgs = $test.Includes | ForEach-Object { '-I' + $_ }
  $output = Join-Path $testOut ($test.Name + '.exe')
  $gccArgs = @('-std=c11','-Wall','-Wextra','-Werror') + @($includeArgs) + @($test.Sources) + @('-o',$output)
  & gcc @gccArgs
  if ($LASTEXITCODE -ne 0) { throw "compile failed: $($test.Name)" }
  & $output
  if ($LASTEXITCODE -ne 0) { throw "test failed: $($test.Name)" }
}
```

Expected: 18 executables compile with `-Werror` and exit 0.

- [ ] **Step 2: Run all Python contracts and both firmware builds**

```powershell
python -m pytest tests -q
cmake --preset Debug
cmake --build --preset Debug
cmake --preset Release
cmake --build --preset Release
```

Expected: all pytest cases pass and both `stem-hub.elf` artifacts link.

- [ ] **Step 3: Restore and verify host master**

```powershell
git -C D:/Codes/STM32/stem-hub-host show-ref --verify refs/heads/codex/transparent-mode
git -C D:/Codes/STM32/stem-hub-host switch master
git -C D:/Codes/STM32/stem-hub-host reset --keep ac74860
& 'D:/Codes/STM32/stem-hub-host/env/release/Scripts/python.exe' -m pytest tests -q
```

Expected: `codex/transparent-mode` still points to `82d9037`, host `master` points to `ac74860`, the worktree is clean, and the full host suite passes.

### Task 8: Flash and verify the connected board

**Files:**
- Create: `docs/power-resistor-bypass-hardware-test-2026-08-23.md`

- [ ] **Step 1: Discover exact tools and connected identities**

Use the STM32 flash skill to locate STM32CubeProgrammer, confirm the ST-Link probe and target, and re-enumerate serial ports. Expected current observations are an STM32 STLink with VID 0483/PID 3748 and FTDI COM12; live discovery is authoritative.

- [ ] **Step 2: Program, verify, and reset the Debug ELF**

Use STM32CubeProgrammer CLI SWD mode and the exact discovered probe to download `D:/Codes/STM32/stem-hub/build/Debug/stem-hub.elf`, verify it, and reset/run the target. Capture the complete CLI result in the hardware record.

- [ ] **Step 3: Run inactive and active hardware scenarios**

```powershell
python tools/resistor_bypass_hardware_test.py --port COM12
python tools/resistor_bypass_hardware_test.py --port COM12 --exercise-loads
```

Expected: handshake succeeds; both inactive ON attempts return `ERROR:STATE`; FWD and REV accept PC13 ON only after startup and remain responsive to `AT+MOTOR?`; STOP and reversal clear PC13; charge ON accepts PC14 ON only in the actual ON phase and the periodic OFF boundary clears it. The script's `finally` cleanup returns both bypasses low and stops both loads.

- [ ] **Step 4: Confirm GPIOC ODR where tooling permits**

Read GPIOC ODR at `0x4001100C` through ST-Link at each safe checkpoint. Bit 13 represents PC13 and bit 14 represents PC14. Record the raw values and decoded bits. If halting the target perturbs timing or the probe cannot read reliably while running, record protocol/state evidence separately and do not claim direct electrical verification.

- [ ] **Step 5: Write and commit the hardware record**

Include firmware commit, ELF hash, programmer version, probe ID, target ID, serial adapter/port, exact commands, responses, ODR evidence, cleanup, and any unverified condition.

```powershell
git add -- docs/power-resistor-bypass-hardware-test-2026-08-23.md
git commit -m "docs: record resistor bypass hardware verification"
```

### Task 9: Merge to the restored firmware master and audit completion

**Files:**
- Git refs in both repositories.

- [ ] **Step 1: Confirm the feature branch and preservation refs**

```powershell
git status --short --branch
git log --oneline --decorate -8
git show-ref --verify refs/heads/codex/transparent-mode
git -C D:/Codes/STM32/stem-hub-host show-ref --verify refs/heads/codex/transparent-mode
```

Expected: only the user's two `.settings` files may remain modified; no feature file is uncommitted.

- [ ] **Step 2: Restore firmware master and merge without deleting branches**

```powershell
git switch master
git reset --keep 4b5eb84
git merge --no-ff codex/power-resistor-bypass -m "merge: add resistor bypass safety controls"
```

Expected: merge succeeds, `master` contains the feature, and both `codex/transparent-mode` and `codex/power-resistor-bypass` remain present.

- [ ] **Step 3: Re-run merge-result verification**

```powershell
python -m pytest tests -q
cmake --build --preset Debug
cmake --build --preset Release
git diff --check HEAD^ HEAD
git status --short --branch
git -C D:/Codes/STM32/stem-hub-host status --short --branch
```

Expected: tests/builds pass; firmware status contains only the preserved `.settings` modifications; host master is clean at `ac74860`.

- [ ] **Step 4: Requirement-by-requirement audit**

Verify current refs and files prove: transparent branches retained; both masters restored to the correct 9600 lineage; PC13/PC14 push-pull low defaults; two commands and error behavior; motor/charge automatic safety resets; documentation and hardware record present; real ST-Link/RS-485 results pass; feature merged; no branch deleted; no user setting committed.
