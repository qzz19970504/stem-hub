# LM51770 Charge Duty Cycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fixed 10-second-on/50-second-off MCU-owned LM51770 charge cycle without changing the AT command set or host UI.

**Architecture:** A platform-independent absolute-deadline state machine emits requested physical power modes. The existing output task owns that state machine, waits on its queue only until the next deadline, and applies every transition through the existing interlock sequencer.

**Tech Stack:** STM32F103, C11, CMSIS-RTOS2/FreeRTOS, CMake/Ninja/GNU Arm, MinGW native tests, STM32CubeProgrammer/ST-Link, UART1 COM12, Python/PySide6/pytest.

---

### Task 1: Lock the scheduler behavior with native tests

**Files:**
- Create: `App/Inc/app_charge_cycle.h`
- Create: `App/Src/app_charge_cycle.c`
- Create: `tests/test_charge_cycle.c`

- [ ] Write tests for first CHARGE, 10/50 transitions, duplicate CHARGE, OFF/DRIVE cancellation, unchanged deadline across unrelated work, and `UINT32_MAX` wrap.
- [ ] Compile before implementation and require failure because the API is missing.
- [ ] Implement `AppChargeCycle`, `AppChargeCycleAction`, request/poll/wait functions, and signed-difference deadline handling.
- [ ] Recompile with `-Wall -Wextra -Werror` and require exit code 0.
- [ ] Commit the state machine and tests.

### Task 2: Integrate the scheduler into the output task

**Files:**
- Modify: `App/Inc/app_config.h`
- Modify: `App/Src/app_output_task.c`
- Modify: `cmake/stm32cubemx/CMakeLists.txt`

- [ ] Add 10000 ms ON and 50000 ms OFF constants and bump the firmware to `release-v3.1`.
- [ ] Convert milliseconds to kernel ticks with ceiling arithmetic and a 64-bit intermediate.
- [ ] Poll expired deadlines before waiting; use the scheduler's remaining ticks as the queue timeout.
- [ ] Route power requests through the scheduler and every returned mode through `App_PowerPathApply()`.
- [ ] Leave direct NMOS1/NMOS2 requests independent without changing the scheduler deadline.
- [ ] Run native tests and a Debug firmware build, then commit.

### Task 3: Synchronize current documentation

**Files:**
- Modify: `README.md`
- Modify: `上位机AT命令文档.md`
- Modify: `钻杆mcu控制功能.md`
- Modify: `D:\Codes\STM32\stem-hub-host\README.md`
- Modify: `D:\Codes\STM32\stem-hub-host\docs\power-path-at-contract.md`

- [ ] Document v3.1, the 10/50 automatic cycle, duplicate-ON idempotence, immediate cancellation, UI meaning, and absence of temperature/current protection.
- [ ] State that this is temporary derating and that full-power validation is excluded.
- [ ] Run `git diff --check` and protocol wording searches, then commit documentation in each repository.

### Task 4: Full verification and hardware timing

- [ ] Compile and run every MCU native test, including `test_charge_cycle`.
- [ ] Run a clean Debug build and record RAM/Flash use.
- [ ] Run the complete host pytest suite; do not rebuild the executable because host code is unchanged.
- [ ] Flash the final ELF and verify download/reset.
- [ ] With the power stage isolated, verify immediate ON, duplicate ON at 5 seconds, OFF by 11 seconds, still OFF before 60 seconds, ON after 60 seconds, and immediate POWER OFF/DRIVE cancellation using COM12 and HOTPLUG GPIO reads.

### Task 5: Review and merge

- [ ] Review both repository diffs and fix all Critical/Important findings.
- [ ] Merge both feature branches into `master`.
- [ ] Re-run MCU tests/build and host pytest on the merged tips.
- [ ] Flash the merged MCU ELF, leave the final GPIO state OFF, and delete both feature branches.
