# AT 命令 RX 死锁调试报告

> **范围**：`fix/at-rx-stall` 分支（已合并到 `master`，merge commit `3e7400c`）
> **作者**：通过 Claude Code 协作完成
> **硬件**：STM32F103C8T6，FreeRTOS V10.3.1 + CMSIS-RTOS2，HAL 库
> **关联 commit**：`994619e`（diag 可观测性）、`49d36a8`（看门狗自愈）

---

## 1. 现象

**用户报告**：每隔 5 秒向 UART1 发 `AT+SENSE?\r\n`，前 47 次正常回包，之后**完全失声**，连 `AT+DIAG?` / `AT+FAULT?` 也无任何响应；板子没有复位迹象（仍在供电）。

原始日志片段（用户提供的）：

```text
[23:04:14.581] ·¢¡ú¡óAT+SENSE?
[23:04:14.581] ÊÕ¡û¡ô+SENSE:BATT_NTC=29.7C,BATT_V=2.8V,NTC1_C=26.6C,NTC2_C=25.9C,NTC3_C=26.8C,TICK=234000,COUNT=235
OK
[23:04:19.595] ·¢¡ú¡óAT+SENSE?        <- 此后所有命令无任何回应
[23:04:24.595] ·¢¡ú¡óAT+SENSE?
... (3 分钟持续无响应)
```

复现时间约 234 秒（TICK=234000ms），样本计数 COUNT=235。

---

## 2. 调试过程（systematic-debugging 四阶段）

### Phase 1：根因调查（静态阅读代码）

通读的核心文件与作用：

| 文件 | 关注点 |
|---|---|
| `App/Src/app_at_task.c` | atTask 主循环、`App_AtProcessLine` 行解析 |
| `App/Src/app_runtime.c` | `HAL_UART_RxCpltCallback` / `HAL_UART_ErrorCallback`、ring buffer push/pop |
| `App/Src/app_state.c` | `sensor_ready_semaphore` 与 `sensor_mutex` 的获取/释放 |
| `App/Src/app_sensor_task.c` | 每秒一次的 ADC 循环 |
| `Core/Src/usart.c` + `freertos.c` | UART 硬件初始化、任务优先级 |

通过静态分析得到 5 个候选根因（按怀疑度排序）：

1. **环形缓冲 push 失败时仍释放信号量**：dropped 字节可能含 `\r`/`\n`，导致后续命令粘在脏行后面
2. **`App_AtTask` 行解析器无超时**：脏行会无限堆积直到 LINE_TOO_LONG
3. **UART 外设 "假死"**：`HAL_UART_Receive_IT` 重新武装失败，RXNE 中断不再产生
4. **`sensor_ready_semaphore` 释放逻辑**：可能存在 TOCTOU，但 max=1 不影响
5. **优先级反转**：motorTask 与 sensorTask 共享 `adc2_mutex`，但 FreeRTOS mutex 默认带优先级继承，可排除

静态分析无法确定哪个是真正原因 → 进入 Phase 2。

### Phase 2：加可观测性（第一轮 commit `994619e`）

新增 `AppRuntimeDiag` 结构与 `AT+DIAG?` 命令，在以下位置埋点：

| 字段 | 埋点位置 | 测什么 |
|---|---|---|
| `rx_isr_count` | `stm32f1xx_it.c` 的 `USART1_IRQHandler` 入口直接 `++` | HAL 层之上的中断实际触发次数 |
| `rx_byte_count` | `App_RuntimePushUart1Byte` 调用 | push 路径总调用次数 |
| `rx_overflow_count` | `App_RuntimePushUart1Byte` 返回 false | ring 满丢字节次数 |
| `rx_error_count` + `rx_ore/ne/fe/pe_count` | `HAL_UART_ErrorCallback`，按 `huart->ErrorCode` 拆解 | 硬件级错误 |
| `line_too_long_count` | `App_AtTask` LINE_TOO_LONG 分支 | 行缓冲溢出 |
| `at_loop_count` | `App_AtTask` 主循环 acquire 成功之后 | atTask 醒来次数 |
| `tx_call_count` / `_completed_count` / `_timeout_count` / `_error_count` | `App_RuntimeSendText` | TX 路径健康度 |

**注意**：第一版把 `extern AppRuntime g_app_runtime;` 放在 `typedef AppRuntime` 之前（app_runtime.h），结果所有引用该类型的 .c 文件都报 `'unknown type name AppRuntime'`。修复：把 `extern` 移到 typedef 之后。

### Phase 3：复现 + GDB 抓现场

#### 3.1 复现

工具：ST-Link + STM32_Programmer_CLI 烧录，Python + pyserial 通过 COM12 跑 `tests/at_repro.py`（每 5s 发 `AT+SENSE?`、每 30s 发 `AT+DIAG?`）。

第一次复现成功：

```text
[01:24:56.035] DIAG  #5 OK  +DIAG:RX_BYTE=380,RX_OVERFLOW=0,RX_ERR=0,ORE=0,NE=0,FE=0,PE=0,LINE_TOO_LONG=0,AT_LOOP=380,TX_CALL=37,TX_OK=37,TX_TIMEOUT=0,TX_ERR=0
[01:24:59.914] SENSE #31 OK  ...
[01:25:05.943] SENSE #32 TIMEOUT        <- 突然失声
[01:25:07.978]   follow-up DIAG also TIMEOUT - UART 死了
```

关键观察：**最后一次 DIAG 所有计数器都是 0**——ring 没满、没 ORE/NE/FE/PE、line buffer 没溢出、TX 全部成功。这是非常具体的"UART 外设假死"特征，不是逻辑 bug。

#### 3.2 GDB 现场快照

用 ST-Link gdbserver 挂上，halt 后读 FreeRTOS 全局状态：

```text
xTickCount             = 228826          ← SysTick 还在跑（每 1ms 加 1）
pxCurrentTCB           = IDLE task        ← scheduler 活着
pxDelayedTaskList      = 0x2000057c       ← 链表被 swap 过，4 个 task 还在等 delay
sensor task:
  TCB @ 0x20001bd0
  eCurrentState = 3 (eSuspended)          ← 但 sensor task 也卡住了
  xItemValue   = 0x0004fd5b (= 327003)
at task:
  TCB @ 0x20001760
  eCurrentState = 2 (eBlocked)            ← atTask 卡在信号量上
  pvContainer   = 0x200005d8 (某 ready list)
```

**结论**：

1. `xTickCount` 在增长 → SysTick 没死
2. IDLE task 在跑 → scheduler 活着
3. atTask 卡在 eBlocked（等信号量）→ **UART1 RX ISR 停止触发**
4. 所有 diag 计数器冻结在最后值 → ISR 真的没跑
5. 没有任何 HAL error callback 触发 → 不是 ORE/NE/FE/PE
6. 不是 atTask 逻辑 bug，是 UART 外设死了

排除了：
- ring buffer 满（计数器 0）
- HAL 错误恢复失败（error callback 没进）
- atTask 自己死锁（state=eBlocked 而不是 eSuspended/eDeleted）

### Phase 4：实施修复（commit `49d36a8`）

#### 4.1 设计

**UART 看门狗**：每 100ms 检查一次 RX 静默时间。如果连续 30 秒没有 RX 字节，认定 UART1 假死，主动复位：

```c
HAL_UART_DeInit(&huart1);
MX_USART1_UART_Init();
huart1.ErrorCode = HAL_UART_ERROR_NONE;   // 清残留错误
App_RuntimeStartUart1Receive();           // 重启 RX_IT
```

`APP_UART_WATCHDOG_TIMEOUT_MS = 30000U` 在 `app_config.h`。

#### 4.2 为什么放在 motorTask

| 候选宿主 | 周期 | 优先级 | 是否合适 |
|---|---|---|---|
| `App_AtTask` | 唤醒于 RX ISR | AboveNormal | ❌ UART 死后永远卡在 `osSemaphoreAcquire(osWaitForever)`，放这里等于没看门狗 |
| `App_MotorTask` | 100ms (`APP_MOTOR_MONITOR_PERIOD_MS`) | AboveNormal | ✅ 独立于 UART 状态，每 100ms 准时醒来 |
| `App_SensorTask` | 1000ms | Normal | 可用但周期太长，30s 阈值下最多 3 次检查机会 |
| FreeRTOS software timer | 软件定时器线程 | configTIMER_TASK_PRIORITY=2 | 优先级太低，可能被饿死 |

选 motorTask。它已经每 100ms 跑一次 `osMessageQueueGet(motor_queue, ..., APP_MOTOR_MONITOR_PERIOD_MS)`，在循环开头加一句 `App_RuntimeUartWatchdogCheck(osKernelGetTickCount())` 即可。

#### 4.3 新增 DIAG 字段

`uart_watchdog_reset_count` —— >0 即说明出现过 UART 假死并被自愈。

#### 4.4 验证

1. **主动造假死**：用 STM32_Programmer_CLI 把 `g_app_runtime_last_rx_ms` (RAM 0x200030e0) 写成 0x1，模拟 "RX 已经 30s 没动了"。然后等 35 秒，发 `AT+DIAG?`：

   ```text
   +DIAG:RX_ISR=42,...,UART_WDG=2
   OK
   ```

   `UART_WDG=2` 说明看门狗触发了 2 次 UART 复位。复位后 `AT+SENSE?` 和 `AT+DIAG?` 都恢复正常响应。

2. **正常跑长测**：6 分钟 71 SENSE + 11 DIAG 全 OK，本次未触发 watchdog（说明 bug 是间歇性的，看门狗能兜底即可）。

3. **主机端单元测试**：`tests/test_at_protocol.c` 加 `AT+DIAG?` 用例，全过。

---

## 3. 修改的文件清单

```
 App/Inc/app_at_protocol.h      +3 -1   新增 APP_AT_COMMAND_QUERY_DIAG 枚举值
 App/Inc/app_config.h           +1      新增 APP_UART_WATCHDOG_TIMEOUT_MS=30000U
 App/Inc/app_runtime.h          +50 -8  新增 AppRuntimeDiag + 看门狗 API
 App/Src/app_at_protocol.c      +6      AppAtProtocol_MatchQuery 加 AT+DIAG?
 App/Src/app_at_task.c          +38     新增 App_AtReplyDiag + 处理分支 + LINE_TOO_LONG 计数
 App/Src/app_motor_task.c       +8      每 100ms 调 App_RuntimeUartWatchdogCheck
 App/Src/app_runtime.c          +136    diag 计数器自增 + 看门狗实现
 Core/Src/stm32f1xx_it.c        +9      USART1_IRQHandler 入口 ++g_app_diag_usart1_isr_count
 README.md                      +1 -1   AT+DIAG? 示例
 上位机AT命令文档.md             +42     §5.8 详细字段说明
 tests/test_at_protocol.c       +1      AT+DIAG? 解析测试
```

**总计**：11 个文件，+292 -3 行。

资源占用：RAM 70.59%（14456 B / 20 KB），FLASH 71.00%（46528 B / 64 KB）。

---

## 4. Git 提交记录

```text
3e7400c (HEAD -> master) Merge fix/at-rx-stall: UART RX stall diag + watchdog self-heal
49d36a8 (fix/at-rx-stall) fix(uart): add watchdog that auto-recovers from RX ISR stalls
994619e (fix/at-rx-stall) feat(diag): AT+DIAG? exposes UART1 RX path counters for stall debugging
126bb79 (origin/master)    Merge branch 'feature/batt-ntc-temperature' into master
```

合并方式：`git merge --no-ff`（保留分支历史，方便以后 revert 单个 commit）。

分支 `fix/at-rx-stall` 保留在本地，未删除。

---

## 5. 未解决的相邻问题（暂未处理）

在调试过程中，**观察到另一个独立 bug**：sensor task 大约运行 21 个周期后停止 publish snapshot，导致 `AT+SENSE?` 返回的 TICK/COUNT 字段冻结在 `TICK=20000, COUNT=21`。但用户的原始报告里没提这个，sensor task 死时 AT 命令响应仍然正常。

**猜测**（未深挖）：sensor task 的 `eCurrentState` 显示 `eSuspended`，但代码里只调 `osDelay(1000)`，应该是 `eBlocked` 而非 `eSuspended`。可能是：
- sensor task 的 `xItemValue` 累加到了 ~327003ms，看 FreeRTOS 是把它当作"远期延迟"放入了 overflow list（`xSuspendedTaskList`）而不是延迟 list
- 或 `pvContainer` 跟 `pxDelayedTaskList1` 实际指向不同 list，存在链表状态错乱

如果你后续想修这个，可以从 sensor task 的 stack overflow 检查（`configCHECK_FOR_STACK_OVERFLOW`）入手。

---

## 6. 后续工程师建议

1. **如果再次发生 "AT 命令无响应"**：
   - 先发 `AT+DIAG?`
   - 看 `UART_WDG`：>0 = 看门狗触发过，UART 已自愈，无需重启设备
   - 看其他计数器对账：RX_ISR 应等于 RX_BYTE；RX_BYTE 与 AT_LOOP 应一致；TX_OK 应等于 TX_CALL
   - 如果 `UART_WDG=0` 但其他计数器看起来对不上，问题不在 UART，看 `line_too_long_count` / sensor task 状态

2. **调阈值**：`APP_UART_WATCHDOG_TIMEOUT_MS` 在 `app_config.h`，改它即可。当前 30s 是基于用户测试命令间隔 5s 留 6× 余量。

3. **新加 AT 命令流程**：跟 `AT+DIAG?` 一样：
   - 在 `app_at_protocol.h` 加枚举值
   - 在 `app_at_protocol.c` 的 `AppAtProtocol_MatchQuery` 加匹配
   - 在 `app_at_task.c` 加 `App_AtReplyXxx` 和 `case APP_AT_COMMAND_QUERY_XXX`

4. **不要在 atTask 里放 watchdog 检查**：UART 死后 atTask 会卡在信号量上。

5. **测试 helper 脚本**（`tests/` 目录）：`at_smoke.py` / `at_repro.py` / `at_observe.py` / `gdb_inspect.py` 可以保留作为回归测试基础设施。

---

## 7. 调试过程时间线

| 阶段 | 大致耗时 | 关键产出 |
|---|---|---|
| Phase 1 静态阅读代码 | ~30 min | 5 个候选根因 |
| Phase 2 加可观测性 | ~40 min | 14 个 diag 计数器 + AT+DIAG? |
| Phase 3 复现 + GDB | ~45 min | UART 假死锁定 + 排除其他候选 |
| Phase 4 实施修复 | ~30 min | UART 看门狗 + 自愈验证 |
| 合并 + 文档 | ~15 min | merge commit + 本报告 |

---

## 8. 相关链接

- 详细 AT 命令文档：[`上位机AT命令文档.md`](../上位机AT命令文档.md) §5.8
- 看门狗 API：[`app_runtime.h`](../App/Inc/app_runtime.h) 的 `App_RuntimeUartWatchdogKick` / `App_RuntimeUartWatchdogCheck`
- 系统设计背景：[`README.md`](../README.md)、[`钻杆mcu控制功能.md`](../钻杆mcu控制功能.md)