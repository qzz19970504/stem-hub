# Thermal Stop and Configurable Charge Time Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在独立测试分支中实现 NTC1/2/3 过温停机，并允许通过 AT 指令设置 60 秒充电周期内的 ON 时间。

**Architecture:** sensorTask 复用五周期平均温度驱动纯 C 热保护状态机，并通过原有输出/电机队列触发停机；app_state 提供互斥保护的热状态和 RAM 充电配置。nmosTask 继续唯一持有电源 GPIO，并让纯 C 充电调度器在周期边界加载新配置。

**Tech Stack:** C11、STM32 HAL、CMSIS-RTOS2/FreeRTOS、CMake/Ninja、MinGW GCC 本机单测、STM32CubeProgrammer、UART1 AT 协议。

---

## 文件结构

- Create: `App/Inc/app_thermal_guard.h` — 纯 C 过温状态机与保护期间命令许可策略。
- Create: `App/Src/app_thermal_guard.c` — 60.0°C 触发、55.0°C 解除、错误值触发。
- Create: `tests/test_thermal_guard.c` — 阈值、滞回、错误读数和许可策略单测。
- Modify: `App/Inc/app_config.h` — 默认/范围/周期和热阈值常量，版本号不变。
- Modify: `App/Inc/app_at_protocol.h` — CHARGE_TIME 命令类型和秒数字段。
- Modify: `App/Src/app_at_protocol.c` — 严格解析设置和查询命令。
- Modify: `tests/test_at_protocol.c` — 新协议的 RED/GREEN 测试。
- Modify: `App/Inc/app_charge_cycle.h` — 固定总周期、可更新待生效 ON 时长的 API。
- Modify: `App/Src/app_charge_cycle.c` — 周期快照、下一周期生效与 60 秒连续 ON。
- Modify: `tests/test_charge_cycle.c` — 1/59、59/1、60/0、运行中更新和回绕测试。
- Modify: `App/Inc/app_state.h`, `App/Src/app_state.c` — 线程安全热状态与 RAM 充电配置。
- Modify: `App/Inc/app_types.h` — 输出队列热停机请求类型。
- Modify: `App/Inc/app_output.h`, `App/Src/app_output_task.c` — 高优先级热停机与执行端保护。
- Modify: `App/Inc/app_motor.h`, `App/Src/app_motor_task.c` — 高优先级 SLEEP 与执行端保护。
- Modify: `App/Src/app_sensor_task.c` — 采样后更新热保护并触发停机。
- Modify: `App/Src/app_at_task.c` — 设置/查询应答和保护期间命令拒绝。
- Modify: `cmake/stm32cubemx/CMakeLists.txt` — 编译新模块。
- Modify: `README.md`, `上位机AT命令文档.md`, `钻杆mcu控制功能.md` — 测试分支协议和安全语义。

### Task 1: Verify the branch baseline

**Files:**
- Inspect: `.settings/bundles-lock.store.json`
- Inspect: `.settings/bundles.store.json`

- [ ] **Step 1: Confirm branch and preserve unrelated changes**

Run:

```powershell
git branch --show-current
git status --short
```

Expected: branch is `codex/test-thermal-charge-time`; only the two pre-existing `.settings` line-ending changes are modified.

- [ ] **Step 2: Run all existing native tests**

Compile the five module-linked tests with `-std=c11 -Wall -Wextra -Werror -IApp/Inc` and their production source, then compile the four standalone conversion tests. Expected: all nine executables exit 0.

- [ ] **Step 3: Run a clean Debug firmware build**

Run:

```powershell
& 'C:\Users\44575\AppData\Local\stm32cube\bundles\cmake\4.0.1+st.3\bin\cmake.exe' --build build\Debug --clean-first --parallel
```

Expected: exit 0 and memory usage remains within 20 KB RAM / 64 KB Flash.

### Task 2: Add CHARGE_TIME protocol parsing

**Files:**
- Modify: `tests/test_at_protocol.c`
- Modify: `App/Inc/app_at_protocol.h`
- Modify: `App/Src/app_at_protocol.c`

- [ ] **Step 1: Write failing parser tests**

Add assertions equivalent to:

```c
expect_charge_time_set("AT+CHARGE_TIME=1\r\n", 1U);
expect_charge_time_set("AT+CHARGE_TIME=10\r\n", 10U);
expect_charge_time_set("AT+CHARGE_TIME=60\r\n", 60U);
expect_command("AT+CHARGE_TIME=?\r\n", APP_AT_COMMAND_QUERY_CHARGE_TIME);
expect_parse_failure("AT+CHARGE_TIME=0\r\n");
expect_parse_failure("AT+CHARGE_TIME=61\r\n");
expect_parse_failure("AT+CHARGE_TIME=-1\r\n");
expect_parse_failure("AT+CHARGE_TIME=1.5\r\n");
expect_parse_failure("AT+CHARGE_TIME=\r\n");
```

- [ ] **Step 2: Run the parser test and verify RED**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_at_protocol.c App/Src/app_at_protocol.c -o "$env:TEMP\test_at_protocol.exe"
```

Expected: compile failure because the new command types/data field do not exist.

- [ ] **Step 3: Add protocol types and strict decimal parsing**

Add these public shapes:

```c
typedef enum
{
    /* existing values remain in order */
    APP_AT_COMMAND_SET_CHARGE_TIME,
    APP_AT_COMMAND_QUERY_CHARGE_TIME
} AppAtCommandType;

typedef struct
{
    uint32_t seconds;
} AppAtChargeTimeCommand;
```

Add `charge_time` to `AppAtCommand.data`. Implement a digit-by-digit parser that accepts only ASCII digits and returns true only for `APP_CHARGE_MIN_ON_TIME_SECONDS..APP_CHARGE_MAX_ON_TIME_SECONDS`. Match the exact query `AT+CHARGE_TIME=?` before parsing assignments.

- [ ] **Step 4: Run the parser test and verify GREEN**

Run the compile command, then execute the resulting binary. Expected: exit 0 with no warnings.

- [ ] **Step 5: Commit the protocol slice**

```powershell
git add -- App/Inc/app_at_protocol.h App/Src/app_at_protocol.c tests/test_at_protocol.c
git commit -m "feat: parse configurable charge time commands"
```

### Task 3: Make the pure charge scheduler configurable

**Files:**
- Modify: `tests/test_charge_cycle.c`
- Modify: `App/Inc/app_charge_cycle.h`
- Modify: `App/Src/app_charge_cycle.c`
- Modify: `App/Inc/app_config.h`

- [ ] **Step 1: Write failing scheduler tests**

Cover these exact behaviors:

```c
assert(App_ChargeCycleInit(&cycle, 60U, 10U));
assert(App_ChargeCycleConfigureOnTicks(&cycle, 1U));
/* current deadline stays unchanged; next ON snapshots 1 ON / 59 OFF */
assert(App_ChargeCycleConfigureOnTicks(&cycle, 59U));
/* next complete cycle becomes 59 ON / 1 OFF */
assert(App_ChargeCycleConfigureOnTicks(&cycle, 60U));
/* at each 60-tick boundary phase remains ON and apply_mode is false */
assert(!App_ChargeCycleConfigureOnTicks(&cycle, 0U));
assert(!App_ChargeCycleConfigureOnTicks(&cycle, 61U));
```

Retain duplicate CHARGE, OFF/DRIVE cancellation, unrelated-message deadline and `UINT32_MAX` wrap tests.

- [ ] **Step 2: Run the charge test and verify RED**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_charge_cycle.c App/Src/app_charge_cycle.c -o "$env:TEMP\test_charge_cycle.exe"
```

Expected: compile failure for the changed init/configuration API.

- [ ] **Step 3: Implement cycle snapshots**

Use this state shape:

```c
typedef struct
{
    AppChargeCyclePhase phase;
    uint32_t deadline_tick;
    uint32_t cycle_ticks;
    uint32_t configured_on_ticks;
    uint32_t active_off_ticks;
} AppChargeCycle;
```

`App_ChargeCycleInit(cycle, cycle_ticks, on_ticks)` validates `1 <= on_ticks <= cycle_ticks <= INT32_MAX`. `App_ChargeCycleConfigureOnTicks` updates only `configured_on_ticks`. Starting an ON phase snapshots `active_off_ticks = cycle_ticks - configured_on_ticks`. A zero `active_off_ticks` keeps ON active at the cycle boundary and returns no GPIO action; if a smaller setting is pending, the boundary starts its ON deadline without toggling EN.

- [ ] **Step 4: Replace fixed millisecond macros**

Keep `APP_FIRMWARE_VERSION` unchanged and define:

```c
#define APP_CHARGE_CYCLE_TIME_SECONDS 60U
#define APP_CHARGE_DEFAULT_ON_TIME_SECONDS 10U
#define APP_CHARGE_MIN_ON_TIME_SECONDS 1U
#define APP_CHARGE_MAX_ON_TIME_SECONDS 60U
#define APP_THERMAL_TRIP_TEMPERATURE_DECI_C 600
#define APP_THERMAL_CLEAR_TEMPERATURE_DECI_C 550
```

- [ ] **Step 5: Run the scheduler test and verify GREEN**

Expected: compile and execution exit 0 with no warnings.

- [ ] **Step 6: Commit the scheduler slice**

```powershell
git add -- App/Inc/app_config.h App/Inc/app_charge_cycle.h App/Src/app_charge_cycle.c tests/test_charge_cycle.c
git commit -m "feat: configure charge duty per cycle"
```

### Task 4: Add the pure thermal guard and safety policy

**Files:**
- Create: `tests/test_thermal_guard.c`
- Create: `App/Inc/app_thermal_guard.h`
- Create: `App/Src/app_thermal_guard.c`

- [ ] **Step 1: Write the failing thermal tests**

Use an API with `AppThermalGuard`, `AppThermalTransition`, and:

```c
App_ThermalGuardInit(&guard, 600, 550);
assert(App_ThermalGuardUpdate(&guard, 600, 400, 400) == APP_THERMAL_NO_CHANGE);
assert(App_ThermalGuardUpdate(&guard, 601, 400, 400) == APP_THERMAL_TRIPPED);
assert(App_ThermalGuardUpdate(&guard, 550, 551, 550) == APP_THERMAL_NO_CHANGE);
assert(App_ThermalGuardUpdate(&guard, 550, 550, 550) == APP_THERMAL_CLEARED);
assert(App_ThermalGuardUpdate(&guard, INT32_MAX, 400, 400) == APP_THERMAL_TRIPPED);
assert(!App_ThermalAllowsPowerMode(true, APP_POWER_MODE_CHARGE));
assert(App_ThermalAllowsPowerMode(true, APP_POWER_MODE_OFF));
assert(!App_ThermalAllowsOutputState(true, true));
assert(App_ThermalAllowsOutputState(true, false));
assert(!App_ThermalAllowsMotorMode(true, APP_MOTOR_MODE_FORWARD));
assert(App_ThermalAllowsMotorMode(true, APP_MOTOR_MODE_SLEEP));
```

- [ ] **Step 2: Run and verify RED**

Run:

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_thermal_guard.c App/Src/app_thermal_guard.c -o "$env:TEMP\test_thermal_guard.exe"
```

Expected: missing-file or missing-symbol failure.

- [ ] **Step 3: Implement the minimal pure module**

The guard starts inactive. Any `INT32_MAX` or value above trip activates it. While active, it clears only when all three values are valid and `<= clear_temperature_deci_c`. Policy helpers allow only OFF, disabled output, and motor SLEEP while active.

- [ ] **Step 4: Run and verify GREEN**

Expected: compile and execution exit 0 with no warnings.

- [ ] **Step 5: Add the module to firmware CMake and commit**

```powershell
git add -- App/Inc/app_thermal_guard.h App/Src/app_thermal_guard.c tests/test_thermal_guard.c cmake/stm32cubemx/CMakeLists.txt
git commit -m "feat: add NTC thermal protection state machine"
```

### Task 5: Add shared safety/config state and AT behavior

**Files:**
- Modify: `App/Inc/app_state.h`
- Modify: `App/Src/app_state.c`
- Modify: `App/Src/app_at_task.c`

- [ ] **Step 1: Add shared-state APIs**

Add mutex-protected APIs:

```c
bool App_StateSetChargeOnTimeSeconds(uint32_t seconds);
bool App_StateTryGetChargeOnTimeSeconds(uint32_t *seconds);
void App_StateSetThermalProtectionActive(bool active);
bool App_StateTryGetThermalProtectionActive(bool *active);
```

Initialize charge time to `APP_CHARGE_DEFAULT_ON_TIME_SECONDS` and thermal protection to false. Validate charge seconds against the configured min/max before taking the mutex.

- [ ] **Step 2: Add AT replies and command gating**

Implement:

```text
AT+CHARGE_TIME=25 -> OK
AT+CHARGE_TIME=?  -> +CHARGE_TIME:25\r\nOK\r\n
```

Before enqueueing motor/output/power ON commands, read thermal state and apply the pure policy helpers. Return `ERROR:OVER_TEMPERATURE` for disallowed actions and `ERROR:STATE_BUSY` if shared state cannot be read. Keep version reply unchanged.

- [ ] **Step 3: Build firmware to verify integration**

Run the Debug build. Expected: exit 0; no version-string change.

- [ ] **Step 4: Commit shared state and AT behavior**

```powershell
git add -- App/Inc/app_state.h App/Src/app_state.c App/Src/app_at_task.c
git commit -m "feat: expose charge timing and thermal command guard"
```

### Task 6: Wire high-priority thermal shutdown through owner tasks

**Files:**
- Modify: `App/Inc/app_types.h`
- Modify: `App/Inc/app_output.h`
- Modify: `App/Src/app_output_task.c`
- Modify: `App/Inc/app_motor.h`
- Modify: `App/Src/app_motor_task.c`
- Modify: `App/Src/app_sensor_task.c`

- [ ] **Step 1: Add one atomic output stop request**

Add `APP_OUTPUT_REQUEST_THERMAL_STOP`. Implement `App_OutputEnqueueThermalStop()` using queue priority 1 and `osWaitForever`. In nmosTask, handle it by requesting `APP_POWER_MODE_OFF`, applying the power-path action, and directly applying NMOS1=false and NMOS2=false through the existing owner-task helper.

- [ ] **Step 2: Add the motor thermal stop path**

Implement `App_MotorEnqueueThermalStop()` as a priority-1, wait-forever enqueue of `APP_MOTOR_MODE_SLEEP`.

- [ ] **Step 3: Enforce thermal state at the consumers**

Before nmosTask applies an enabling target or CHARGE/DRIVE, read thermal state and force/retain OFF if active or unreadable. Before motorTask applies any non-SLEEP request, read thermal state and apply SLEEP if active or unreadable. OFF/SLEEP always remain executable.

- [ ] **Step 4: Drive protection from sensorTask**

Declare a static `AppThermalGuard`. After producing the five-cycle average snapshot, call `App_ThermalGuardUpdate` with `next_snapshot.ntc1/2/3.physical_value`, store its active state, and on `APP_THERMAL_TRIPPED` call output thermal stop first and motor thermal stop second. `APP_THERMAL_CLEARED` changes only shared permission state and does not enqueue restart commands.

- [ ] **Step 5: Load configurable timing in nmosTask**

Convert 60 seconds and the configured ON seconds to ticks. Before a CHARGE request and before each due phase transition, load the latest charge seconds from app_state and call `App_ChargeCycleConfigureOnTicks`. If configuration cannot be read or converted, force OFF and enter `Error_Handler` rather than use an unsafe duration.

- [ ] **Step 6: Run all native tests and firmware build**

Expected: ten native tests pass (the original nine plus thermal guard), Debug firmware links, and no task/stack declaration changes appear in `Core/Src/freertos.c`.

- [ ] **Step 7: Commit task integration**

```powershell
git add -- App/Inc/app_types.h App/Inc/app_output.h App/Src/app_output_task.c App/Inc/app_motor.h App/Src/app_motor_task.c App/Src/app_sensor_task.c
git commit -m "feat: stop outputs on NTC overtemperature"
```

### Task 7: Update MCU documentation

**Files:**
- Modify: `README.md`
- Modify: `上位机AT命令文档.md`
- Modify: `钻杆mcu控制功能.md`

- [ ] **Step 1: Document test-branch behavior**

Document the two CHARGE_TIME commands, 1..60 range, default 10, RAM-only setting, next-cycle semantics, 60-second continuous mode, response format, 60.0/55.0°C hysteresis, complete stop scope, `ERROR:OVER_TEMPERATURE`, five-cycle-average response delay, unchanged `release-v3.1`, and absence of host changes.

- [ ] **Step 2: Check documentation consistency**

Run:

```powershell
rg -n "CHARGE_TIME|OVER_TEMPERATURE|60\.0|55\.0|release-v3\.1" README.md 上位机AT命令文档.md 钻杆mcu控制功能.md
git diff --check
```

Expected: all semantics appear consistently and no whitespace errors are reported.

- [ ] **Step 3: Commit documentation**

```powershell
git add -- README.md 上位机AT命令文档.md 钻杆mcu控制功能.md
git commit -m "docs: describe thermal stop test firmware"
```

### Task 8: Final software and hardware verification

**Files:**
- Verify: `build/Debug/stem-hub.elf`

- [ ] **Step 1: Run fresh full native verification**

Compile all module-linked and standalone tests with `-Werror`, execute all ten binaries, and require exit 0.

- [ ] **Step 2: Run fresh clean Debug build**

Require exit 0 and record RAM/Flash percentages. Confirm `git diff master -- Core/Src/freertos.c` is empty and `APP_FIRMWARE_VERSION` is still `release-v3.1`.

- [ ] **Step 3: Perform independent code review**

Review the complete branch diff against the approved design, focusing on queue ordering, stale queued starts, threshold units, `INT32_MAX`, zero-OFF behavior, Tick wrap, and unchanged host/version scope. Resolve all Critical/Important findings before flashing.

- [ ] **Step 4: Flash and identify the actual UART port**

Use STM32CubeProgrammer with SWD under reset to program and verify `build/Debug/stem-hub.elf`. Enumerate Windows serial ports rather than assuming the prior COM number.

- [ ] **Step 5: Verify AT behavior without a charging load**

Confirm:

```text
AT+VERSION?       -> +VERSION:release-v3.1
AT+CHARGE_TIME=?  -> +CHARGE_TIME:10
AT+CHARGE_TIME=0  -> ERROR:PARSE
AT+CHARGE_TIME=61 -> ERROR:PARSE
AT+CHARGE_TIME=1  -> OK, then query 1
AT+CHARGE_TIME=60 -> OK, then query 60
```

- [ ] **Step 6: Verify GPIO timing with ST-Link HOTPLUG**

With no real charging load, read GPIOA ODR at `0x4001080C` and GPIOB ODR at `0x40010C0C`:

- For `n=1`, verify PA8=1/PB3=0 initially, PA8=1/PB3=1 after about 2 seconds, and the next ON starts near 60 seconds.
- For `n=60`, verify PA8=1/PB3=0 initially and remains so across the 60-second boundary without a visible OFF/restart.

- [ ] **Step 7: Return hardware to safe state**

Send `AT+POWER=OFF`, `AT+NMOS1=OFF`, `AT+NMOS2=OFF`, and `AT+MOTOR=SLEEP`; verify PA8=1/PB3=1. Do not claim a heated-NTC end-to-end test unless the sensors were physically and safely heated.

- [ ] **Step 8: Preserve the test branch**

Confirm the current branch remains `codex/test-thermal-charge-time`, the two `.settings` files remain uncommitted, the worktree has no other changes, and do not merge or delete the branch.
