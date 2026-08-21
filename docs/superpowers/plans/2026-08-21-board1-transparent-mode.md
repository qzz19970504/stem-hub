# Board 1 Exclusive Transparent Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give board 1 the same mutually exclusive AT/transparent transfer behavior as board 2, including `AT+TRANS=1/2/1&2` and a lossless guarded `+++` exit.

**Architecture:** Keep UART2/UART3 and all safety tasks on their existing paths. Convert only UART1 to 64-byte Receive-to-Idle chunks, attach idle metadata to each chunk, and route chunks through either the CRLF AT line reader or the board-2-derived transparent escape state machine. Serialize bridge selection, clearing, flushing, and reverse-event publication with one mutex.

**Tech Stack:** STM32F103 HAL, CMSIS-RTOS2/FreeRTOS, C11, MinGW host tests, CMake/Ninja/GNU Arm, STM32CubeProgrammer, ST-Link, UART/RS485 at 9600 8N1.

---

### Task 1: Replace bridge switch commands with transparent entry commands

**Files:**
- Modify: `App/Inc/app_at_protocol.h`
- Modify: `App/Src/app_at_protocol.c`
- Modify: `tests/test_at_protocol.c`

- [ ] **Step 1: Write failing parser assertions**

Add a helper that requires `APP_AT_COMMAND_START_TRANSPARENT` and its exact
target, then add these assertions without changing existing motor, power,
query, or `AT+UARTTX` assertions:

```c
static void expect_transparent(const char *line, AppBridgeTarget target)
{
    AppAtCommand command = {0};
    assert(AppAtProtocol_Parse(line, &command));
    assert(command.type == APP_AT_COMMAND_START_TRANSPARENT);
    assert(command.data.transparent.target == target);
}

expect_transparent("AT+TRANS=1\r\n", APP_BRIDGE_TARGET_UART2);
expect_transparent("AT+TRANS=2\r\n", APP_BRIDGE_TARGET_UART3);
expect_transparent("AT+TRANS=1&2\r\n", APP_BRIDGE_TARGET_UART23);
assert(!AppAtProtocol_Parse("AT+TRANS=\r\n", &command));
assert(!AppAtProtocol_Parse("AT+TRANS=3\r\n", &command));
assert(!AppAtProtocol_Parse("AT+UART2=ON\r\n", &command));
assert(!AppAtProtocol_Parse("AT+UART2=OFF\r\n", &command));
assert(!AppAtProtocol_Parse("AT+UART3=ON\r\n", &command));
assert(!AppAtProtocol_Parse("AT+UART3=OFF\r\n", &command));
assert(!AppAtProtocol_Parse("AT+UART2&3=ON\r\n", &command));
assert(!AppAtProtocol_Parse("AT+UART2&3=OFF\r\n", &command));
assert(!AppAtProtocol_Parse("AT+UART23=ON\r\n", &command));
assert(!AppAtProtocol_Parse("AT+UART23=OFF\r\n", &command));
```

- [ ] **Step 2: Run the focused test and prove RED**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc `
  tests/test_at_protocol.c App/Src/app_at_protocol.c `
  -o "$env:TEMP\test_board1_at_protocol.exe"
```

Expected: compilation fails because `APP_AT_COMMAND_START_TRANSPARENT` and
`data.transparent` do not exist.

- [ ] **Step 3: Implement exact `AT+TRANS` parsing**

Replace the bridge command type and payload with:

```c
APP_AT_COMMAND_START_TRANSPARENT,

typedef struct
{
    AppBridgeTarget target;
} AppAtTransparentCommand;
```

Use `AppAtTransparentCommand transparent` in the command union. In the parser,
remove all UART ON/OFF branches and add one exact value matcher:

```c
if (AppAtProtocol_MatchAssignment("AT+TRANS=", command_body, &value))
{
    out_command->type = APP_AT_COMMAND_START_TRANSPARENT;
    if (strcmp(value, "1") == 0)
        out_command->data.transparent.target = APP_BRIDGE_TARGET_UART2;
    else if (strcmp(value, "2") == 0)
        out_command->data.transparent.target = APP_BRIDGE_TARGET_UART3;
    else if (strcmp(value, "1&2") == 0)
        out_command->data.transparent.target = APP_BRIDGE_TARGET_UART23;
    else
        return false;
    return true;
}
```

- [ ] **Step 4: Run parser regression and commit**

Run the Step 2 command and executable. Expected: exit 0. Commit only the three
parser files:

```powershell
git add App/Inc/app_at_protocol.h App/Src/app_at_protocol.c tests/test_at_protocol.c
git commit -m "feat: replace bridge switches with transparent entry"
```

### Task 2: Add a tested CRLF line reader

**Files:**
- Create: `App/Inc/app_line_reader.h`
- Create: `App/Src/app_line_reader.c`
- Create: `tests/test_line_reader.c`

- [ ] **Step 1: Add the board-2-compatible line-reader test**

Test complete CRLF lines, overflow discard through the next CRLF, recovery with
a following line, embedded NUL preservation by length, invalid initialization,
and reset. The public interface is:

```c
typedef enum {
    APP_LINE_READER_NONE = 0,
    APP_LINE_READER_COMPLETE,
    APP_LINE_READER_TOO_LONG
} AppLineReaderStatus;

bool AppLineReader_Init(AppLineReader *reader, char *buffer, size_t capacity);
AppLineReaderStatus AppLineReader_Push(AppLineReader *reader, uint8_t byte);
const char *AppLineReader_GetLine(const AppLineReader *reader);
size_t AppLineReader_GetLineLength(const AppLineReader *reader);
void AppLineReader_Reset(AppLineReader *reader);
```

- [ ] **Step 2: Prove RED, implement, and prove GREEN**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc `
  tests/test_line_reader.c App/Src/app_line_reader.c `
  -o "$env:TEMP\test_board1_line_reader.exe"
```

Expected before implementation: missing header/source. Implement the bounded
caller-owned line accumulator with the interface above. Expected afterward:
exit 0.

- [ ] **Step 3: Commit the isolated module**

```powershell
git add App/Inc/app_line_reader.h App/Src/app_line_reader.c tests/test_line_reader.c
git commit -m "feat: add reusable UART line reader"
```

### Task 3: Add lossless escape detection and UART chunk metadata

**Files:**
- Modify: `App/Inc/app_config.h`
- Create: `App/Inc/app_transparent_mode.h`
- Create: `App/Src/app_transparent_mode.c`
- Create: `App/Inc/app_uart_chunk_queue.h`
- Create: `App/Src/app_uart_chunk_queue.c`
- Create: `tests/test_transparent_mode.c`
- Create: `tests/test_uart_chunk_queue.c`

- [ ] **Step 1: Add constants and failing tests**

Add:

```c
#define APP_UART1_RX_CHUNK_SIZE 64U
#define APP_UART1_CHUNK_QUEUE_CAPACITY 8U
```

Port the board-2 test matrices. Transparent-mode tests must cover all three
targets, AT-looking payload, binary NUL payload, exact guarded escape, missing
pre-guard, missing post-guard followed by another byte, `abc+++def`, `++++`,
`++X`, fragmented `+`/`+`/`+`, abort, re-entry, and invalid arguments. Queue
tests must cover FIFO order, wraparound, capacity overflow, reset, and invalid
arguments.

- [ ] **Step 2: Prove both tests RED**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc `
  tests/test_transparent_mode.c App/Src/app_transparent_mode.c `
  -o "$env:TEMP\test_board1_transparent_mode.exe"
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc `
  tests/test_uart_chunk_queue.c App/Src/app_uart_chunk_queue.c `
  -o "$env:TEMP\test_board1_uart_chunk_queue.exe"
```

Expected: missing headers/sources.

- [ ] **Step 3: Implement the two pure modules**

Use these public types and functions:

```c
typedef struct {
    bool active;
    AppBridgeTarget target;
    uint8_t escape_candidate[3];
    size_t escape_length;
    bool escape_has_pre_guard;
} AppTransparentMode;

typedef struct {
    uint8_t forward[APP_UART1_RX_CHUNK_SIZE + 3U];
    size_t forward_length;
    bool exited;
} AppTransparentResult;

bool AppTransparentMode_ProcessChunk(AppTransparentMode *mode,
    const uint8_t *bytes, size_t length, bool silence_before,
    bool silence_after, AppTransparentResult *result);

typedef struct {
    uint16_t length;
    bool silence_before;
    bool silence_after;
} AppUartChunk;
```

The escape implementation holds at most three plus bytes and only consumes
them when both guards are true. Every failed candidate is copied to `forward`
before later bytes. The SPSC queue uses monotonically increasing head/tail
sequences and latches overflow until reset.

- [ ] **Step 4: Prove GREEN and commit**

Run both Step 2 executables. Expected: exit 0.

```powershell
git add App/Inc/app_config.h App/Inc/app_transparent_mode.h `
  App/Src/app_transparent_mode.c App/Inc/app_uart_chunk_queue.h `
  App/Src/app_uart_chunk_queue.c tests/test_transparent_mode.c `
  tests/test_uart_chunk_queue.c
git commit -m "feat: add guarded transparent escape state machine"
```

### Task 4: Make bridge selection exact and race-free

**Files:**
- Modify: `App/Inc/app_state.h`
- Modify: `App/Src/app_state.c`
- Modify: `App/Inc/app_runtime.h`
- Modify: `App/Src/app_runtime.c`
- Modify: `App/Src/app_bridge_task.c`
- Modify: `tests/test_app_state.c`
- Modify: `tests/stubs/app_runtime.h`

- [ ] **Step 1: Add failing state-selection tests**

Add assertions that each selection replaces the previous pair and that clear
disables both:

```c
App_StateSelectBridgeTarget(APP_BRIDGE_TARGET_UART2);
App_StateGetBridgeEnabled(&uart2, &uart3);
assert(uart2 && !uart3);
App_StateSelectBridgeTarget(APP_BRIDGE_TARGET_UART3);
App_StateGetBridgeEnabled(&uart2, &uart3);
assert(!uart2 && uart3);
App_StateSelectBridgeTarget(APP_BRIDGE_TARGET_UART23);
App_StateGetBridgeEnabled(&uart2, &uart3);
assert(uart2 && uart3);
App_StateClearBridgeTarget();
App_StateGetBridgeEnabled(&uart2, &uart3);
assert(!uart2 && !uart3);
```

- [ ] **Step 2: Prove RED and add exact state APIs**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -IApp/Inc `
  tests/test_app_state.c App/Src/app_state.c `
  -o "$env:TEMP\test_board1_app_state.exe"
```

Expected: missing `App_StateSelectBridgeTarget` and
`App_StateClearBridgeTarget`. Replace the incremental setter with exact select
and clear operations under `state_mutex`, then rerun for exit 0.

- [ ] **Step 3: Add bridge transaction serialization**

Add `bridge_mutex` to the real and stub runtime structures; create it and
fail-fast with the other RTOS objects. Add:

```c
void App_RuntimeLockBridge(void);
void App_RuntimeUnlockBridge(void);
void App_RuntimeSelectBridgeTarget(AppBridgeTarget target);
void App_RuntimeClearBridgeTarget(void);
```

Selection and clearing hold `bridge_mutex`, change the exact state, flush both
downstream RX rings, and release it. Refactor `App_BridgeDrainUart` so one lock
covers checking the active state, consuming bytes, encoding/sending the event,
and flushing disabled input.

- [ ] **Step 4: Build focused state test and commit**

Run the Step 2 command and executable again, then compile the firmware later in
Task 7 to validate HAL/RTOS integration.

```powershell
git add App/Inc/app_state.h App/Src/app_state.c App/Inc/app_runtime.h `
  App/Src/app_runtime.c App/Src/app_bridge_task.c tests/test_app_state.c `
  tests/stubs/app_runtime.h
git commit -m "feat: serialize exact transparent target selection"
```

### Task 5: Preserve UART1 idle chunks in the existing runtime

**Files:**
- Modify: `App/Inc/app_runtime.h`
- Modify: `App/Src/app_runtime.c`

- [ ] **Step 1: Define the UART1 chunk API**

Replace the public per-byte pop operation with:

```c
bool App_RuntimePopUart1Chunk(uint8_t *bytes, size_t capacity,
    size_t *length, bool *silence_before, bool *silence_after);
bool App_RuntimeConsumeUart1Overflow(void);
```

Store a 64-byte UART1 receive buffer in `AppRuntime`; keep the chunk queue,
next-pre-guard flag, and overflow latch private to `app_runtime.c`.

- [ ] **Step 2: Convert only UART1 to Receive-to-Idle**

Arm UART1 with:

```c
HAL_UARTEx_ReceiveToIdle_IT(&huart1,
    g_app_runtime.uart1_rx_chunk,
    sizeof(g_app_runtime.uart1_rx_chunk));
```

In `HAL_UARTEx_RxEventCallback`, push every received UART1 byte into the
existing UART1 ring, push one matching metadata entry, set `silence_after` only
for `HAL_UART_RXEVENT_IDLE`, release the UART1 semaphore once, update the next
pre-guard flag, and rearm. Remove the USART1 branch from
`HAL_UART_RxCpltCallback`; leave UART2/UART3 byte reception unchanged.

- [ ] **Step 3: Implement coherent overflow recovery and diagnostics**

If either ring insertion or metadata insertion fails, latch overflow and keep
the existing diagnostic counters. `App_RuntimeConsumeUart1Overflow` enters a
critical section, resets ring and queue together, restores initial silence,
and clears the latch. UART1 error callback keeps ORE/NE/FE/PE counts, aborts the
Receive-to-Idle operation, restores initial silence, and rearms it. UART2/3
keep their current error recovery.

- [ ] **Step 4: Compile firmware and commit**

Use the bundled CMake/Ninja/toolchain paths to build Debug. Expected: all new
HAL symbols resolve and RAM/Flash remain within regions.

```powershell
git add App/Inc/app_runtime.h App/Src/app_runtime.c
git commit -m "feat: preserve UART1 idle chunk boundaries"
```

### Task 6: Route UART1 exclusively through AT or transparent mode

**Files:**
- Modify: `App/Src/app_at_task.c`
- Modify: `cmake/stm32cubemx/CMakeLists.txt`
- Add contract test: `tests/test_transparent_task_contract.py`

- [ ] **Step 1: Add a failing source contract**

The Python test reads real sources and requires:

```python
assert "AppTransparentMode_IsActive" in at_task
assert "App_RuntimePopUart1Chunk" in at_task
assert "AppLineReader_Push" in at_task
assert "APP_AT_COMMAND_START_TRANSPARENT" in at_task
assert "App_AtForwardLine" not in at_task
assert "App_RuntimePopUart1Byte" not in at_task
assert "HAL_UARTEx_RxEventCallback" in runtime
assert "HAL_UART_Receive_IT(&huart1" not in runtime
```

Also require all three new module sources in the CubeMX CMake source list.

- [ ] **Step 2: Prove the contract RED**

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' `
  -m pytest tests/test_transparent_task_contract.py -q
```

Expected: failures for missing exclusive routing and new sources.

- [ ] **Step 3: Implement exclusive routing**

Make the transparent state and receive chunk static to protect the AT task's
stack. `App_AtTask` waits once per chunk, consumes overflow first, then drains
all chunk metadata. AT-mode chunks feed `AppLineReader`; transparent chunks
call `AppTransparentMode_ProcessChunk` and forward `result.forward` unchanged
to the exact state-selected targets.

On `APP_AT_COMMAND_START_TRANSPARENT`, send `OK`, select the exact target, and
enter the mode. On valid escape, clear targets and send `OK`. On overflow,
abort the mode, clear targets, reset the line reader, and send
`ERROR:RX_OVERFLOW`. If a command and payload share one receive chunk, route
all remaining bytes as transparent data with no artificial pre-guard.

Remove legacy non-AT line forwarding. Keep all existing command handlers and
thermal command guards unchanged. `AT+UARTTX` continues to use the current
payload handler and therefore returns `ERROR:UART_DISABLED` in AT mode.

- [ ] **Step 4: Prove contract and focused tests GREEN, then commit**

Run the Python command, parser test, line-reader test, transparent-mode test,
queue test, and app-state test. Expected: all exit 0.

```powershell
git add App/Src/app_at_task.c cmake/stm32cubemx/CMakeLists.txt `
  tests/test_transparent_task_contract.py
git commit -m "feat: separate board1 AT and transparent modes"
```

### Task 7: Update protocol documentation and run full regression

**Files:**
- Modify: `README.md`
- Modify: `上位机AT命令文档.md`

- [ ] **Step 1: Replace the old bridge documentation**

Document the three `AT+TRANS` values, exclusive modes, 1 ms guards, lossless
failed escape behavior, UART2/UART3 reverse hex events, disabled AT commands
during transparency, and the requirement to exit before manual motor/power
control. Remove examples that say non-AT lines are forwarded while AT remains
available. Preserve all unrelated motor, power, sensing, protection, and
diagnostic sections.

- [ ] **Step 2: Run all host tests**

Compile and run the existing 17 native C mappings plus the four new native
tests with `-Wall -Wextra -Werror`. Run:

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest tests -q
```

Expected: 21 C executables exit 0 and all Python tests pass.

- [ ] **Step 3: Build Debug and Release cleanly**

Use bundled CMake 4.0.1, Ninja 1.13.1, and GNU Arm 13.3.1 with process-local
PATH and explicit `CMAKE_MAKE_PROGRAM`. Build both presets with `--clean-first`.
Expected: both links fit 20 KiB RAM and 63 KiB Flash. Record sizes and inspect
AT-task static/stack usage if either region overflows.

- [ ] **Step 4: Commit docs after verification**

```powershell
git add README.md 上位机AT命令文档.md
git commit -m "docs: document exclusive transparent transfer"
```

### Task 8: Flash, identify, exercise hardware, and merge

**Files:**
- Verify: `build/Debug/stem-hub.elf`

- [ ] **Step 1: Program and verify through ST-Link**

Use the STM32 flash skill with the explicit Debug ELF. Require probe serial
`37FF71064E573436947D1143`, target STM32F101/F102/F103 medium density,
successful download verification, and reset.

- [ ] **Step 2: Test UART3 on the currently connected COM10**

Open COM12 and COM10 at 9600 8N1. Verify `AT+TRANS=2`, exact raw/binary forward,
`AT+VERSION?` as payload with no AT response, `abc+++def`, `++++`, missing guard,
valid guarded `+++`, no escape leakage, UART3 reverse event, restored
`AT+VERSION?`, old command rejection, and representative `AT+SENSE?` or
`AT+DIAG?` after exit.

- [ ] **Step 3: Move the adapter and verify UART2 plus both dual sides**

Ask the user to move COM10 to UART2. Verify `AT+TRANS=1` in both directions and
the UART2 side of `AT+TRANS=1&2`; then move it back to UART3 and verify the
UART3 side of the same dual-target command. Exit after every case.

- [ ] **Step 4: Audit and merge**

Run all host tests and clean Debug/Release builds once more, `git diff --check`,
confirm only the user's two `.settings` files remain unstaged, and audit every
design requirement. Merge `codex/transparent-mode` to `master` with `--no-ff`,
rerun tests/builds on merged `master`, keep the feature branch, and never stage
the `.settings` files.
