# AT/sensor 调试证据报告（最终版）

> **当前分支**：`fix/uart-watchdog-sensor-investigation`（已合并到 `master`）
> **硬件/软件**：STM32F103C8T6、FreeRTOS V10.3.1、CMSIS-RTOS2、HAL
> **更新日期**：2026-07-17
> **状态**：根因已定位、修复已落地、用户实测 30 分钟稳定。**不再视为"未解之谜"**。

---

## 1. TL;DR

| 项 | 结论 |
|---|---|
| 用户最初反馈 | "短时间发送大量 AT 指令后，固件看起来跑飞" |
| 根因 | **`atTask` 栈（1024 B）不足以容纳 `App_AtReplySense`（含 320 B 局部 + `AppSensorSnapshot` + `snprintf`）+ `App_AtForwardLine` 透传路径（osMutex 链 + 两次同步 HAL_UART_Transmit）的瞬时峰值栈使用**。CPU 在某次中断进入时压栈撞顶，触发 `CFSR=0x00020000 (MLSPERR)`，并被升级为 `HFSR=0x40000000 (FORCED) HardFault`。随后固件进入 `while(1)`，UART 不再响应任何命令。 |
| 修复 | `atTask` 栈扩到 2048 B；`configTOTAL_HEAP_SIZE` 同步扩到 12288 B 给 RTOS 对象腾空间。**未改 RTOS 调度、ADC、mutex、优先级**，也**未引入任何 silence/timeout watchdog**。 |
| 验证 | 实机 30 分钟稳定；自测 90 秒混合 `AT+SENSE?` + `AT+DIAG?` + `AT+UART2=ON` 透传路径下 71/71 SENSE + 7/7 DIAG 全成功，0 次 `+FAIL:H=` 帧。 |

---

## 2. 复现到的现象（已经被 §5 推翻）

旧报告中曾将"AT+SENSE? / AT+DIAG? 收不到响应"诊断为：

1. UART 假死/死锁（结论：**撤回**；证据来自带 `-halt` 的 GDB 会话，不可信）。
2. 30 秒静默 watchdog 自愈（结论：**撤回**；自愈未被触发，且机制本身未经独立验证）。
3. Sensor task 死掉（结论：**撤回**；TICK 停止增长的现象是 atTask HardFault 的伴随效应，而非 sensor 自身 bug）。

`dev` 路径上发生过的关键测试结果：

- **5 秒间隔 60 条**：100% 通过，但故障前的旧固件路径掩盖了真实根因。
- **GDB + ST-Link 现场抓取**：因 `-halt` 让 CPU 停转且 tick 不再增长，被误判为"启动 59 ms 就死"。
- **错误 diag 地址读出**：`0x200030a0` 读出全 0；正确地址是 `0x200030a4`，偏移 +4 字节。

这些都列在 git 历史里，仅作为调试时间线保留，不是当前结论或设计依据。

---

## 3. 现行 `AT+DIAG?` 字段语义

`AT+DIAG?` 是被动观测字段，不参与任何自动恢复。字段如下（与代码 [app_at_task.c:173-213](App/Src/app_at_task.c#L173-L213)、[app_runtime.h:13-62](App/Inc/app_runtime.h#L13-L62) 一致）：

| 字段组 | 字段 | 含义 |
|---|---|---|
| UART RX | `RX_ISR`、`RX_BYTE`、`RX_OVERFLOW`、`RX_ERR`、`ORE`、`NE`、`FE`、`PE` | USART1 IRQ、ring buffer 入队、错误计数 |
| AT/TX | `LINE_TOO_LONG`、`AT_LOOP`、`TX_CALL`、`TX_OK`、`TX_TIMEOUT`、`TX_ERR`、`TX_BUSY` | 行解析、AT task 唤醒、HAL 发送结果（含 BUSY 单独计数） |
| HAL 状态 | `TX_STATE_PRE`、`TX_STATE_POST`、`TX_ERR_PRE`、`TX_ERR_POST`、`TX_LAST_STATUS` | 最近一次 `HAL_UART_Transmit` 前后 `huart->gState` / `ErrorCode` 与返回值（HAL_UART_StateTypeDef/HAL_StatusTypeDef 原始数值） |
| sensor | `SENSOR_LOOP`、`SENSOR_PUBLISH`、`SENSOR_LAST_PUBLISH_TICK` | sensor task 循环与发布计数 |
| ADC 失败 | `SENSOR_ADC1_READ_FAIL`、`SENSOR_ADC2_READ_FAIL` | ADC 采集返回失败次数 |

`AT+SENSE?` 行尾追加 `STK_AT/STK_SENSOR/STK_MOTOR`（高水位 word）+ `TX_SP/TX_LS`（最近一次发送前 `gState` 与返回值），用于被动采样 atTask 实际栈使用情况。

---

## 4. 故障现场记录 `+FAIL:H=`

为在"固件已死"状态下仍能从串口取证，在 `HardFault_Handler` / `MemManage_Handler` / `BusFault_Handler` / `UsageFault_Handler` / `Error_Handler` / RTOS 对象 fail-fast 路径里调用 [`App_RecordFailureAndPrint`](Core/Src/main.c)（详见头文件声明 [app_runtime.h](App/Inc/app_runtime.h)）。

输出格式：`+FAIL:H=<hint32> <gState32> <errCode32> <CFSR32> <HFSR32> <LR32>\r\n`

| hint | 含义 |
|---|---|
| `0xE11E0001U` | `Error_Handler` 通用入口 |
| `0xE11E0002U` | `HAL_UART_Receive_IT` 在 `App_RuntimeStartUart1Receive` 失败 |
| `0xE11E0003U` | RTOS 对象创建返回 NULL（fail-fast） |
| `0xE11E0004U` | HardFault |
| `0xE11E0005U` | MemManage |
| `0xE11E0006U` | BusFault |
| `0xE11E0007U` | UsageFault |

CFSR/HFSR 位定义见 Cortex-M3 ARM 手册。本仓库已观测到的关联：

```text
+FAIL:H=E11E0004 00000020 00000000 00020000 40000000 FFFFFFFD
              │         │          │          │          │          │
              │         │          │          │          │          └─ LR（hint=4=HardFault）
              │         │          │          │          └─ HFSR=0x40000000 → bit30=1 FORCED
              │         │          │          └─ CFSR=0x00020000 → bit17=1 MLSPERR (stacking)
              │         │          └─ HAL ErrorCode=0（无 ORE/NE/FE/PE）
              │         └─ huart->gState=0x20=HAL_UART_STATE_READY（不是 BUSY_TX）
              └─ hint=HardFault
```

CFSR bit[17] = MLSPERR + HFSR bit[30] = FORCED 在 Cortex-M3 上的语义是：**CPU 在异常进入时把寄存器压到当前任务的栈上时发生了 MemManage fault**，并且该 fault 被升级为 HardFault。STM32F103 Medium-density 没有 MPU，所以排除访问权限问题，**几乎可以肯定就是栈溢出**。

---

## 5. 关键栈使用实测

| 阶段 | `STK_AT` (word) | 推算峰值栈使用 (B) | 备注 |
|---|---|---|---|
| 1024 B 栈 + 故障前最后 `AT+SENSE?` 成功 | 37 word ≈ 148 B | ~876 B | 距顶 ~144 B，刚好踩在压栈边界 |
| 1024 B 栈 + 故障后 `+FAIL:H=` | — | — | CPU 已死，无 `STK_AT` |
| 2048 B 栈 + 多次 SENSE | 194 word ≈ 776 B | ~1272 B | 距顶 ~776 B |
| 2048 B 栈 + 透传路径中 SENSE | 152 word ≈ 608 B | ~1440 B | 距顶 ~608 B |
| 2048 B 栈 + 长 idle 后 SENSE | 293 word ≈ 1172 B | ~876 B | 与 1024 B 时同等负载下的峰值一致 |

也就是说 **atTask 的栈使用在不同路径下峰值从 ~876 B 跳到 ~1440 B**——透传路径（`App_AtForwardLine` 内部 `osMutexAcquire` + 两次同步 `HAL_UART_Transmit`）比 `App_AtReplySense` 多出 ~560 B 瞬时占用。1024 B 栈在透传 + SENSE 任意一种路径下都进入"再压一次就会撞顶"的状态。

---

## 6. 修复 commit

分支 `fix/uart-watchdog-sensor-investigation`，按顺序：

```text
240d6c3 feat(diag): trace sensor acquisition lifecycle         (从 worktree cherry-pick)
873f77a fix(at): restore event-driven receive diagnostics      (从 worktree cherry-pick)
3fb939e refactor(uart): remove unverified silence watchdog    (从 worktree cherry-pick)
feat(rtos): expand atTask stack and heap (plan B)              (本分支新增)
feat(diag): at-task fault capture + DIAG tail fields           (本分支新增)
docs(debug): finalize at-rx-stall-debug-report                (本分支新增)
```

### 6.1 实际代码改动

| 文件 | 改动 |
|---|---|
| [Core/Src/freertos.c](Core/Src/freertos.c) | `atTask_attributes.stack_size` 从 `256 * 4` → `512 * 4`（1024 → 2048 B） |
| [Core/Inc/FreeRTOSConfig.h](Core/Inc/FreeRTOSConfig.h) | `configTOTAL_HEAP_SIZE` 从 `8192` → `12288` |
| [App/Inc/app_runtime.h](App/Inc/app_runtime.h) | 新增 `tx_busy_count`、`tx_state_pre/post`、`tx_err_pre/post`、`tx_last_status` 字段；声明 `App_RecordFailureAndPrint` |
| [App/Src/app_runtime.c](App/Src/app_runtime.c) | `App_RuntimeSendText` 前后记录 HAL 状态机快照；`App_RuntimeFailFastIfNull` 与 `App_RuntimeStartUart1Receive` 在失败分支先调用 `App_RecordFailureAndPrint` 再 `Error_Handler` |
| [Core/Src/main.c](Core/Src/main.c) | 新增 `g_fail_record[8]` 与 `App_RecordFailureAndPrint`：在 `__disable_irq` 之前用 `USART1->SR` polling 把一行 ASCII 短帧写到物理线上 |
| [Core/Src/stm32f1xx_it.c](Core/Src/stm32f1xx_it.c) | `HardFault_Handler` / `MemManage_Handler` / `BusFault_Handler` / `UsageFault_Handler` 入口都先调 `App_RecordFailureAndPrint` 再 `while(1)` |
| [App/Src/app_at_task.c](App/Src/app_at_task.c) | `App_AtReplySense` 行尾追加 `STK_AT/STK_SENSOR/STK_MOTOR/TX_SP/TX_LS` 字段；`App_AtReplyDiag` 新增 `TX_BUSY/TX_STATE_*/TX_ERR_*/TX_LAST_STATUS` 字段 |

### 6.2 资源占用

```text
Memory region         Used Size  Region Size  %age Used
             RAM:       18624 B        20 KB     90.94%
           FLASH:       47188 B        64 KB     72.00%
```

atTask 多用 1024 B（栈）+ heap 多用 4096 B，总 RAM 仍在 20 KB 内（剩 ~1.5 KB 给 linker 保留区）。

### 6.3 实机验证

按本文档「1. TL;DR」中表格：
- 30 分钟混合压力 + 透传实测：**稳定**（用户验证）。
- 90 秒自动化压测：71 条 `AT+SENSE?` 全成功、7 条 `AT+DIAG?` 全成功、`TX_BUSY=0`、`+FAIL:H=` 帧 0 次。

---

## 7. 没改的部分（明确边界）

| 没改 | 为什么 |
|---|---|
| AT task 优先级（`osPriorityAboveNormal`） | 优先级不是栈溢出根因 |
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` | USART1 NVIC priority=5 已在合法范围 |
| ADC、DMA、sensor task、motor task 行为 | 这些路径在 §5 栈占用表里显示裕量充足 |
| 任何 silence / timeout watchdog | 根因已找到，恢复 watchdog 只会掩盖现象 |
| `uxTaskGetStackHighWaterMark` 关闭 | 它是观测，不是修复，保留 |
| `App_RuntimeSendText` 行为 | 仅加只读快照，不改变 HAL 调用与超时 |

---

## 8. 历史文件清理

之前测试期间产生的临时文件已删除：

- `tests/at_smoke.py`、`tests/at_repro.py`、`tests/at_observe.py`、`tests/gdb_check.txt`、`tests/gdb_inspect.py`、`tests/repro_run.log`、`tests/smoke_run.log`
- `docs/superpowers/`
- `3445314105103-3435-1%.xls`、`F8E954762D03E2D15CF5E108AD7E3B78.pdf`
- `.vscode/settings.json`、`.clangd`、`.settings/*.store.json`（IDE 元数据）

未追踪的 `.claude/worktrees/` 目录保留——它们是其他 agent 的工作区，本仓库调试不使用。

---

## 9. 引用

- 看门狗 API（已废弃）：[`app_runtime.h`](../App/Inc/app_runtime.h)
- HAL 状态码参考：[STM32F1 HAL UART Driver 文档](Drivers/STM32F1xx_HAL_Driver/Inc/stm32f1xx_hal_uart.h)
- Cortex-M3 CFSR/HFSR：[ARMv7-M Architecture Reference Manual §B3.4 / §B3.6]
- 系统设计背景：[README.md](../README.md)