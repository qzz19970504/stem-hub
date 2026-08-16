# Motor Stall Protection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add corrected 220-ohm motor-current conversion, 4.0 A persistent stall protection, additive AT configuration, and verified STM32/RS-485 behavior.

**Architecture:** Pure C modules own IPROPI conversion, stall timing, and persisted-record validation. The existing motor task remains the only motor-pin owner, shared state publishes the active threshold, the AT task performs idle-only configuration writes, and one linker-reserved STM32F103 flash page stores a CRC-protected record.

**Tech Stack:** C11, STM32F103 HAL, CMSIS-RTOS2/FreeRTOS, CMake/Ninja/arm-none-eabi-gcc, MinGW GCC native tests, Python contract tests, STM32CubeProgrammer, ST-Link, UART1 RS-485 AT protocol.

---

## File map

- Create `App/Inc/app_motor_current.h` and `App/Src/app_motor_current.c`: real production IPROPI conversion and deci-amp formatting clamp.
- Create `App/Inc/app_motor_stall_guard.h` and `App/Src/app_motor_stall_guard.c`: HAL-free blanking and persistence state machine.
- Create `App/Inc/app_stall_config.h`, `App/Src/app_stall_config.c`, and `App/Src/app_stall_config_flash.c`: record validation/CRC plus the single-page HAL persistence boundary.
- Create `App/Inc/app_stall_config_service.h` and `App/Src/app_stall_config_service.c`: testable idle/running/no-op/write orchestration used by the AT task.
- Modify `App/Inc/app_config.h`: 220-ohm calibration, timing/default/range constants, and corrected range comments.
- Modify `App/Inc/app_at_protocol.h`, `App/Src/app_at_protocol.c`, and `tests/test_at_protocol.c`: set/query grammar and typed payload.
- Modify `App/Inc/app_state.h`, `App/Src/app_state.c`, and `tests/test_app_state.c`: active threshold shared state.
- Modify `App/Src/app_runtime.c`: load validated persistent configuration before tasks start.
- Modify `App/Src/app_at_task.c`: set/query execution, running guard, Flash error handling.
- Modify `App/Src/app_motor_task.c`: 10 ms monitoring and BRAKE response.
- Modify `App/Src/app_sensor_task.c`: corrected 33.3 A display clamp.
- Modify `cmake/stm32cubemx/CMakeLists.txt` and `STM32F103XX_FLASH.ld`: compile new modules and reserve the last 1 KiB page.
- Replace mirrored `tests/test_motor_current_conversion.c`; create `tests/test_motor_stall_guard.c` and `tests/test_stall_config.c`.
- Modify `README.md`, `上位机AT命令文档.md`, and `钻杆mcu控制功能.md`; create `docs/motor-stall-hardware-test-2026-08-16.md` after hardware execution.

## Task 1: Correct and test the production current conversion

**Files:**
- Create: `App/Inc/app_motor_current.h`
- Create: `App/Src/app_motor_current.c`
- Modify: `App/Inc/app_config.h`
- Modify: `App/Src/app_sensor_task.c`
- Replace: `tests/test_motor_current_conversion.c`
- Modify: `cmake/stm32cubemx/CMakeLists.txt`

- [ ] **Step 1: Replace the mirrored test with a failing production test**

Test the desired public API directly:

```c
#include "app_motor_current.h"

assert(App_MotorCurrentFromMillivolts(0U) == 0U);
assert(App_MotorCurrentFromMillivolts(50U) == 505U);
assert(App_MotorCurrentFromMillivolts(317U) == 3202U);
assert(App_MotorCurrentFromMillivolts(1881U) == 19000U);
assert(App_MotorCurrentFromMillivolts(3300U) == 33333U);
assert(App_MotorCurrentToDeciAmps(33333U) == 333U);
assert(App_MotorCurrentToDeciAmps(40000U) == 333U);
```

- [ ] **Step 2: Run RED and confirm the missing-header failure**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_motor_current_conversion.c App/Src/app_motor_current.c -o "$env:TEMP\test_motor_current_conversion.exe"
```

Expected: compilation fails because `app_motor_current.h` or its functions do not exist.

- [ ] **Step 3: Add the minimal production conversion**

Declare:

```c
uint32_t App_MotorCurrentFromMillivolts(uint32_t millivolts);
uint32_t App_MotorCurrentToDeciAmps(uint32_t current_ma);
```

Implement with 64-bit intermediates and named constants:

```c
uint32_t App_MotorCurrentFromMillivolts(uint32_t millivolts)
{
    const uint64_t denominator =
        ((uint64_t)APP_MOTOR_IPROPI_AIPROPI_UA_PER_A
         * APP_MOTOR_IPROPI_R19_OHMS) / 1000ULL;
    return (uint32_t)(((uint64_t)millivolts * 1000ULL) / denominator);
}

uint32_t App_MotorCurrentToDeciAmps(uint32_t current_ma)
{
    uint32_t current_deci_a = (current_ma + 50U) / 100U;
    return (current_deci_a > APP_MOTOR_CURRENT_MAX_DECI_A)
        ? APP_MOTOR_CURRENT_MAX_DECI_A : current_deci_a;
}
```

Set R19 to `220U`, derive the maximum deci-amp constant as `333U`, and remove every 2.5 kohm/2.9 A comment. Use the production functions from the motor and sensor tasks.

- [ ] **Step 4: Run GREEN**

Compile and run the native test. Expected: exit 0 and the test's final `OK` line.

- [ ] **Step 5: Commit the conversion slice**

```powershell
git add -- App/Inc/app_config.h App/Inc/app_motor_current.h App/Src/app_motor_current.c App/Src/app_motor_task.c App/Src/app_sensor_task.c tests/test_motor_current_conversion.c cmake/stm32cubemx/CMakeLists.txt
git commit -m "fix: calibrate motor current for 220 ohm sense resistor"
```

## Task 2: Implement the test-first stall timing guard

**Files:**
- Create: `App/Inc/app_motor_stall_guard.h`
- Create: `App/Src/app_motor_stall_guard.c`
- Create: `tests/test_motor_stall_guard.c`
- Modify: `App/Inc/app_config.h`
- Modify: `cmake/stm32cubemx/CMakeLists.txt`

- [ ] **Step 1: Write failing behavior tests**

Define a wished-for API and tests for blanking, continuous current, low/invalid reset, restart, and wrap:

```c
AppMotorStallGuard guard;
App_MotorStallGuardStart(&guard, 1000U);
assert(!App_MotorStallGuardUpdate(&guard, 1299U, true, 9000U, 4000U));
assert(!App_MotorStallGuardUpdate(&guard, 1300U, true, 4000U, 4000U));
assert(!App_MotorStallGuardUpdate(&guard, 1399U, true, 4000U, 4000U));
assert(App_MotorStallGuardUpdate(&guard, 1400U, true, 4000U, 4000U));
```

Separate tests assert that a 3999 mA sample, an invalid sample, and `App_MotorStallGuardStop()` clear pending evidence; a start near `UINT32_MAX` behaves identically across wrap.

- [ ] **Step 2: Run RED**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_motor_stall_guard.c App/Src/app_motor_stall_guard.c -o "$env:TEMP\test_motor_stall_guard.exe"
```

Expected: missing-header or missing-symbol failure.

- [ ] **Step 3: Implement the minimal state machine**

Use this public state and API:

```c
typedef struct
{
    uint32_t run_start_tick;
    uint32_t over_threshold_start_tick;
    bool is_running;
    bool is_over_threshold;
} AppMotorStallGuard;

void App_MotorStallGuardStart(AppMotorStallGuard *guard, uint32_t tick);
void App_MotorStallGuardStop(AppMotorStallGuard *guard);
bool App_MotorStallGuardUpdate(AppMotorStallGuard *guard,
                               uint32_t tick,
                               bool is_sample_valid,
                               uint32_t current_ma,
                               uint32_t threshold_ma);
```

Use unsigned subtraction for both `APP_MOTOR_STALL_STARTUP_BLANKING_MS` (`300U`) and `APP_MOTOR_STALL_PERSISTENCE_MS` (`100U`). Equality to threshold counts as over-current.

- [ ] **Step 4: Run GREEN and refactor names only while green**

Expected: every guard scenario exits 0 with no warnings.

- [ ] **Step 5: Commit the pure policy**

```powershell
git add -- App/Inc/app_config.h App/Inc/app_motor_stall_guard.h App/Src/app_motor_stall_guard.c tests/test_motor_stall_guard.c cmake/stm32cubemx/CMakeLists.txt
git commit -m "feat: add motor stall timing guard"
```

## Task 3: Add strict AT grammar and shared threshold state

**Files:**
- Modify: `App/Inc/app_at_protocol.h`
- Modify: `App/Src/app_at_protocol.c`
- Modify: `tests/test_at_protocol.c`
- Modify: `App/Inc/app_state.h`
- Modify: `App/Src/app_state.c`
- Modify: `tests/test_app_state.c`

- [ ] **Step 1: Write RED protocol tests**

Add exact successful parses for `AT+STALL_CURRENT=1000`, `4000`, `30000`, and `AT+STALL_CURRENT=?`. Add individual rejection tests for 999, 30001, empty, negative, decimal, suffix, leading plus, and overflowing digits.

The typed payload is:

```c
typedef struct
{
    uint32_t current_ma;
} AppAtStallCurrentCommand;
```

Expected command types are `APP_AT_COMMAND_SET_STALL_CURRENT` and `APP_AT_COMMAND_QUERY_STALL_CURRENT`.

- [ ] **Step 2: Verify protocol RED, then implement decimal parsing**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_at_protocol.c App/Src/app_at_protocol.c -o "$env:TEMP\test_at_protocol.exe"
```

Implement digit-by-digit overflow-safe parsing by rejecting before multiplication when the next value would exceed `APP_MOTOR_STALL_MAX_CURRENT_MA`. Match the query before assignment parsing.

- [ ] **Step 3: Verify protocol GREEN**

Expected: all existing and new parser assertions pass.

- [ ] **Step 4: Write RED shared-state tests**

Assert the default is 4000 mA, both endpoints are accepted, values outside 1000 to 30000 are rejected without mutation, null output is rejected, and unavailable state mutex returns false.

- [ ] **Step 5: Implement shared-state accessors and verify GREEN**

Add:

```c
bool App_StateSetStallCurrentMa(uint32_t current_ma);
bool App_StateTryGetStallCurrentMa(uint32_t *current_ma);
```

Store `stall_current_ma` in `AppState`, initialized with `APP_MOTOR_STALL_DEFAULT_CURRENT_MA`.

```powershell
gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -IApp/Inc tests/test_app_state.c App/Src/app_state.c -o "$env:TEMP\test_app_state.exe"
& "$env:TEMP\test_app_state.exe"
```

- [ ] **Step 6: Commit protocol and state**

```powershell
git add -- App/Inc/app_at_protocol.h App/Src/app_at_protocol.c tests/test_at_protocol.c App/Inc/app_state.h App/Src/app_state.c tests/test_app_state.c
git commit -m "feat: add configurable stall current protocol"
```

## Task 4: Add CRC-protected Flash persistence

**Files:**
- Create: `App/Inc/app_stall_config.h`
- Create: `App/Src/app_stall_config.c`
- Create: `App/Src/app_stall_config_flash.c`
- Create: `tests/test_stall_config.c`
- Modify: `STM32F103XX_FLASH.ld`
- Modify: `cmake/stm32cubemx/CMakeLists.txt`

- [ ] **Step 1: Write RED record tests**

Test the real record API:

```c
AppStallConfigRecord record;
App_StallConfigBuildRecord(&record, 4000U);
assert(App_StallConfigRecordIsValid(&record));
assert(record.stall_current_ma == 4000U);

record.crc32 ^= 1U;
assert(!App_StallConfigRecordIsValid(&record));
```

Separate tests corrupt magic, format version, reserved, and range; an all-`0xFF` record must be invalid. Verify CRC against a fixed expected value so writer and validator cannot share the same wrong algorithm unnoticed.

- [ ] **Step 2: Run RED**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_stall_config.c App/Src/app_stall_config.c -o "$env:TEMP\test_stall_config.exe"
```

Expected: missing API failure.

- [ ] **Step 3: Implement record build/validation and GREEN**

Use magic `0x53544C31U` (`STL1`), format version `1U`, reserved `0U`, the standard reflected CRC-32 polynomial `0xEDB88320U`, initial value `0xFFFFFFFFU`, and final inversion. Define the 16-byte record exactly as the specification and assert its size at compile time. The fixed 4000 mA record CRC must be `0x59B6320BU` on the STM32 little-endian representation.

- [ ] **Step 4: Add the narrow HAL Flash writer**

Expose:

```c
uint32_t App_StallConfigLoadCurrentMa(void);
bool App_StallConfigStoreCurrentMa(uint32_t current_ma);
```

Read from `0x0800FC00U`. Store by unlocking, erasing exactly one page, programming eight halfwords, locking on all paths, then validating the read-back record. Invalid load returns 4000 mA. Invalid store input returns false without touching Flash.

- [ ] **Step 5: Reserve the page and compile sources**

Change linker Flash length from `64K` to `63K`; add all three new source files to CMake. The page address and linker arithmetic must agree: `0x08000000 + 63 KiB = 0x0800FC00`.

- [ ] **Step 6: Commit persistence**

```powershell
git add -- App/Inc/app_stall_config.h App/Src/app_stall_config.c App/Src/app_stall_config_flash.c tests/test_stall_config.c STM32F103XX_FLASH.ld cmake/stm32cubemx/CMakeLists.txt
git commit -m "feat: persist stall current in flash"
```

## Task 5: Execute AT configuration and initialize persisted state

**Files:**
- Create: `App/Inc/app_stall_config_service.h`
- Create: `App/Src/app_stall_config_service.c`
- Create: `tests/test_stall_config_service.c`
- Modify: `App/Src/app_runtime.c`
- Modify: `App/Src/app_at_task.c`
- Modify: `tests/test_at_command_guard.c`
- Modify: `cmake/stm32cubemx/CMakeLists.txt`

- [ ] **Step 1: Add RED service tests with link-time fakes**

`tests/test_stall_config_service.c` supplies fake definitions of `App_StateTryGetMotorStatus`, `App_StateTryGetStallCurrentMa`, `App_StateSetStallCurrentMa`, and `App_StallConfigStoreCurrentMa`. It tests every result from:

```c
typedef enum
{
    APP_STALL_CONFIG_SET_OK = 0,
    APP_STALL_CONFIG_SET_STATE_BUSY,
    APP_STALL_CONFIG_SET_MOTOR_RUNNING,
    APP_STALL_CONFIG_SET_FLASH_WRITE_FAILED
} AppStallConfigSetResult;

AppStallConfigSetResult App_StallConfigServiceSetCurrentMa(uint32_t current_ma);
```

Cover unavailable motor state, FWD, REV, unavailable current threshold, same-value no-op with zero store calls, failed store with unchanged state, successful store followed by one state update, and state-update failure. Because the persistent write has already succeeded in the last case, retry the shared-state setter once; if both attempts fail, return `STATE_BUSY` and let the next reset load the persisted value.

- [ ] **Step 2: Verify service RED**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -IApp/Inc tests/test_stall_config_service.c App/Src/app_stall_config_service.c -o "$env:TEMP\test_stall_config_service.exe"
```

Expected: missing header/symbol failure.

- [ ] **Step 3: Implement boot load and AT branches**

During `App_RuntimeInit()`, after RTOS objects exist and before tasks start:

```c
if (!App_StateSetStallCurrentMa(App_StallConfigLoadCurrentMa()))
{
    Error_Handler();
}
```

The AT set handler calls the tested service and maps its four results to `OK`, `ERROR:STATE_BUSY`, `ERROR:MOTOR_RUNNING`, or `ERROR:FLASH_WRITE`. Add a dedicated 64-byte query reply buffer that emits `+STALL_CURRENT:<n>\r\nOK\r\n`.

- [ ] **Step 4: Verify GREEN and existing thermal command-guard behavior**

Configuration and queries must remain allowed during over-temperature because neither energizes hardware. Run `test_at_command_guard` after adding the new enum values.

- [ ] **Step 5: Commit AT execution**

```powershell
git add -- App/Inc/app_stall_config_service.h App/Src/app_stall_config_service.c App/Src/app_runtime.c App/Src/app_at_task.c tests/test_stall_config_service.c tests/test_at_command_guard.c cmake/stm32cubemx/CMakeLists.txt
git commit -m "feat: expose persistent stall threshold commands"
```

## Task 6: Integrate the detector into the motor owner task

**Files:**
- Modify: `App/Src/app_motor_task.c`
- Modify: `App/Inc/app_config.h`
- Create or modify: `tests/test_motor_task_contract.py`

- [ ] **Step 1: Write RED integration contract tests**

Assert the source integration includes the real current conversion and guard, uses `APP_MOTOR_MONITOR_PERIOD_MS == 10U`, starts the guard on FWD/REV, stops it on non-running modes and thermal SLEEP, feeds invalid ADC samples as invalid evidence, loads the shared threshold, and stores BRAKE with trip current and `overcurrent_latched=true`.

- [ ] **Step 2: Run RED**

```powershell
python -m pytest tests/test_motor_task_contract.py -q
```

Expected: assertions fail because the task still has fixed 3 A immediate tripping and 100 ms polling.

- [ ] **Step 3: Integrate the guard minimally**

Create one task-local `AppMotorStallGuard`. On every applied mode, start it for FWD/REV and stop it otherwise. Preserve the previous latch for SLEEP/WAKE/BRAKE/STOP; clear it only when applying FWD/REV. During running mode:

```c
bool is_current_valid = App_MotorReadCurrent(&current_ma);
bool did_stall = App_MotorStallGuardUpdate(
    &stall_guard,
    osKernelGetTickCount(),
    is_current_valid,
    current_ma,
    stall_current_ma);
```

If `did_stall`, drive EN low with nSLEEP high, retain PH, stop the guard, and store `BRAKE`, trip current, and latch true. Remove `APP_MOTOR_OVERCURRENT_THRESHOLD_MA` and the old immediate branch.

- [ ] **Step 4: Run GREEN plus focused native tests**

Run the contract, current conversion, stall guard, state, AT protocol, and config tests. Expected: all pass and no compiler warnings.

- [ ] **Step 5: Commit task integration**

```powershell
git add -- App/Inc/app_config.h App/Src/app_motor_task.c tests/test_motor_task_contract.py
git commit -m "feat: stop motor on sustained stall current"
```

## Task 7: Run the complete automated and firmware build verification

**Files:**
- Modify only if a verified failure exposes a root cause in the feature.

- [ ] **Step 1: Run every native test**

Use one explicit temporary output directory and these exact source mappings:

```powershell
$testOut = Join-Path $env:TEMP 'stem-hub-stall-tests'
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
  @{Name='test_stall_config_service'; Sources=@('tests/test_stall_config_service.c','App/Src/app_stall_config_service.c'); Includes=@('tests/stubs','App/Inc')}
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

Expected: 17 executables compile and exit 0.

- [ ] **Step 2: Run Python contracts**

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest tests -q
```

Expected: all collected firmware Python tests pass.

- [ ] **Step 3: Build Debug and Release using the STM32 bundled tools**

Use the flash skill's process-local tool discovery or equivalent bundled paths:

```powershell
& 'C:\Users\44575\.agents\skills\stm32-cubemx-flash\scripts\flash_stm32.ps1' -ProjectRoot (Get-Location).Path -Configuration Debug -Build -ListOnly
& 'C:\Users\44575\.agents\skills\stm32-cubemx-flash\scripts\flash_stm32.ps1' -ProjectRoot (Get-Location).Path -Configuration Release -Build -ListOnly
```

Expected: both builds succeed, each produces one ELF, and the link fits inside 63 KiB Flash and 20 KiB RAM. Do not flash during this task.

- [ ] **Step 4: Inspect diff and commit any evidence-backed corrections**

Use systematic debugging for every failure. Do not alter tests merely to accept current behavior.

## Task 8: Update documentation before hardware execution

**Files:**
- Modify: `README.md`
- Modify: `上位机AT命令文档.md`
- Modify: `钻杆mcu控制功能.md`

- [ ] **Step 1: Update all incorrect calibration and range text**

Replace 2.5 kohm, `/1125`, 2.93 A, and 2.9 A clamp statements with 220 ohm, `/99`, approximately 33.3 A, and 33.3 A clamp. Preserve the distinction between instantaneous motor-task sampling and 1 Hz SENSE publication.

- [ ] **Step 2: Document protection and AT commands**

Add exact command/response/error examples, 1000 to 30000 mA range, 4000 mA default, 300 ms blanking, 100 ms persistence, BRAKE response, unrestricted FWD/REV restart, Flash fallback, and idle-only configuration.

- [ ] **Step 3: Document safety limits**

State that software protection does not replace DRV8874 hardware protection and that verification avoids intentional 19 A hard stalls.

- [ ] **Step 4: Run documentation consistency searches and commit**

```powershell
rg -n "2500|2\.5.?k|1125|2\.9.?A|2933|APP_MOTOR_OVERCURRENT_THRESHOLD" README.md '上位机AT命令文档.md' '钻杆mcu控制功能.md' App tests
git diff --check
```

Expected: no stale calibration/protection references except explicitly labeled historical material.

```powershell
git add -- README.md '上位机AT命令文档.md' '钻杆mcu控制功能.md'
git commit -m "docs: document persistent motor stall protection"
```

## Task 9: Flash and verify through ST-Link and RS-485

**Files:**
- Create: `docs/motor-stall-hardware-test-2026-08-16.md`

- [ ] **Step 1: Discover the exact freshly built image and probe**

```powershell
& 'C:\Users\44575\.agents\skills\stm32-cubemx-flash\scripts\flash_stm32.ps1' -ProjectRoot (Get-Location).Path -Configuration Debug -ListOnly
```

Expected: the worktree Debug ELF, CubeProgrammer 2.23.0, and probe `37FF71064E573436947D1143`.

- [ ] **Step 2: Program, verify, and reset**

```powershell
& 'C:\Users\44575\.agents\skills\stm32-cubemx-flash\scripts\flash_stm32.ps1' -ProjectRoot (Get-Location).Path -Configuration Debug
```

Expected: `Download verified successfully`, followed by reset.

- [ ] **Step 3: Identify the RS-485 COM port and run non-destructive AT checks**

Use `Get-CimInstance Win32_SerialPort` and a short Python serial helper at 115200 8N1 with CRLF. Record exact request/response bytes for VERSION, STALL_CURRENT query, MOTOR query, and idle threshold set.

- [ ] **Step 4: Verify persistence**

Set 4200 mA while stopped, reset through CubeProgrammer or power-cycle, reconnect, and confirm 4200. Then set a safe test threshold while stopped.

- [ ] **Step 5: Verify running rejection and trip/restart behavior**

Run FWD or REV, verify configuration returns `ERROR:MOTOR_RUNNING`, and observe current. With a controlled load and the lowest allowed threshold needed to cross current safely, verify BRAKE and `OVERCURRENT=1`; retry either direction and verify it is allowed and can trip again.

- [ ] **Step 6: Restore the handoff state**

Send SLEEP, restore 4000 mA, reset, and query both motor and threshold. Leave the motor de-energized.

- [ ] **Step 7: Write and commit the hardware record**

Record firmware commit/ELF hash, probe, COM port, every AT transcript, observed current, persistence result, trip timing, and any step that could not be safely produced.

```powershell
git add -- docs/motor-stall-hardware-test-2026-08-16.md
git commit -m "test: record motor stall hardware verification"
```

## Task 10: Final verification and merge to master

**Files:**
- No new files expected.

- [ ] **Step 1: Apply verification-before-completion on the feature branch**

Re-run all native tests, Python tests, Debug/Release builds, `git diff --check`, `git status --short`, and the requirement-by-requirement audit against the approved design.

- [ ] **Step 2: Merge from the primary checkout without staging user files**

From `D:\Codes\STM32\stem-hub`, confirm the only pre-existing changes are the two `.settings` files. Merge explicitly:

```powershell
git merge --no-ff codex/motor-stall-protection -m "merge: add persistent motor stall protection"
```

- [ ] **Step 3: Re-verify the merge result**

Run the focused native tests and Debug build from `master`. Confirm the merge did not include `.settings` changes and that the documented hardware evidence references the merged feature commits.

- [ ] **Step 4: Report exact final evidence**

Report master commit, feature commits, tests/build counts, programmed ELF, ST-Link serial, RS-485 persistence/trip results, final 4000 mA setting, and any explicitly unverified physical condition. Only then mark the persistent goal complete.
