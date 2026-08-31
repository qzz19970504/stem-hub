# 上位机 AT 命令文档

本文档是 `release-v3.3` AT 控制面的精确协议参考，面向上位机开发、联调和测试人员。

> 适用固件：`release-v3.3`
> 接口要点：固件按固定顺序发送语义化 SENSE 和 OUTPUT 字段；上位机必须精确匹配 `release-v3.3` 握手，并严格要求完整、无重复的字段集合。
> 产品状态模型、完整流程、产品级门控和保护恢复见：[stem-hub模块集成与使用说明](./stem-hub模块集成与使用说明.md)。现场操作见：[stem-hub模块操作说明](./stem-hub模块操作说明.md)。

## 1. 文档范围

本文档是命令语法、参数范围、大小写与结束符、同步响应、异步事件、错误码和兼容写法的唯一权威，不包含未来规划。

当前已覆盖：

- UART1 AT 命令收发
- UART2 和 UART3 透传开关控制
- 通过 `AT+UARTTX=<HEX>` 向 UART2/UART3 精确发送二进制数据
- UART2/UART3 接收数据通过异步十六进制事件回传
- LED 总开关控制
- 电机模式控制与状态查询
- 电机启动限流电阻旁路
- NMOS1、NMOS2 控制
- 充电路径与驱动路径控制
- 充电限流电阻旁路
- 完整输出状态查询与 DRIVE 子输出强制联锁
- 60 秒周期内的充电开启时间设置与查询
- 传感采样结果查询
- 故障状态查询

本文档不展开产品介绍、完整操作流程、RTOS 任务、硬件规格、产品级门控或保护恢复策略；请使用上述两份产品文档。

## 2. 串口通信约定

### 2.1 串口参数

UART1、UART2、UART3 当前都使用相同配置：

| 参数 | 值 |
| --- | --- |
| 波特率 | 9600 |
| 数据位 | 8 |
| 校验位 | None |
| 停止位 | 1 |
| 流控 | None |

### 2.2 AT 命令入口

- UART1 是唯一的 AT 命令入口。
- UART2 和 UART3 不解析 AT；它们是受 UART1 控制的下游收发通道。
- UART2/UART3 的接收数据以异步事件回传，格式见第 6 节。

### 2.3 行结束符

上位机必须使用 CRLF，也就是 `\r\n`，作为一条 AT 命令的结束符。例如：

```text
AT+LED=ON\r\n
```

除明确显示 `\r\n` 的示例外，本文代码块中的命令文本均省略了可见结束符；在线上发送时仍必须追加一个 CRLF。

AT 控制帧只在收到 CRLF 后才形成。未以 `\r\n` 结束的数据仍在接收缓冲中，固件不会立即给出 AT 错误回包；只有 `\r` 或只有 `\n` 也不会形成 AT 控制帧。上位机不得把下列格式作为错误探测手段：

```text
AT+LED=ON
AT + LED = ON
AT+LED = ON\r\n
AT+LED=ON\n
```

其中含空格或制表符的完整行不会通过 AT 候选帧筛选，可能进入兼容透传路径；是否产生下游数据取决于桥接状态。它们不是有效 AT 命令，也不应期待 `ERROR:PARSE`。

### 2.3.1 长度边界与 `LINE_TOO_LONG` 恢复

- 一般 AT 候选帧的协议校验要求**总长度小于 96 字节**，长度包含 CRLF。总长度为 96～127 字节的完整普通行不会成为候选 AT 帧，而是进入兼容透传路径。
- UART1 行缓冲可累积 127 个字节；收到第 128 个字节时立即返回 `ERROR:LINE_TOO_LONG` 并清零缓冲。它不会进入“丢弃直到 CRLF”状态，溢出后的后续字节会作为新行的开头，因此残余字节可能形成另一条命令或兼容透传行。
- 精确前缀 `AT+UARTTX=` 在候选帧筛选前走专门分支：即使总长度为 96～127 字节，只要该行最终以 CRLF 形成，解析失败也返回 `ERROR:HEX`，而不是透传或 `ERROR:PARSE`。

上位机必须在发送端限制一条完整行的长度。收到 `ERROR:LINE_TOO_LONG` 后：停止发送新请求，确保当前发送已经结束并以 CRLF 收尾，排空 UART1 上的残余终止响应和异步事件，再重新执行 `AT+VERSION?` 握手与状态查询同步。若不能确定残余数据没有进入已启用的下游桥接，必须按下游协议执行恢复，或复位设备；不要简单截断后重发。

### 2.4 大小写规则

当前解析器是严格大小写敏感的，上位机必须发送大写命令字和值：

```text
AT+MOTOR=FWD\r\n
```

含小写字母的完整行同样不会通过 AT 候选帧筛选，可能进入兼容透传路径，而不是返回 `ERROR:PARSE`：

```text
at+motor=fwd
AT+motor=FWD\r\n
```

### 2.5 一次只发一条命令

建议每条命令单独成行并等待回包后再发送下一条，尤其是在电机控制和状态读取混用时。

注意：串行请求并不意味着接收线上只有当前命令的回包。桥接启用后，`+UART2RX:` / `+UART3RX:` 异步事件可能穿插出现，上位机必须独立分流。

## 3. 通信模型

### 3.1 请求-应答模型

AT 控制面采用串口请求-应答模型：

1. 上位机通过 UART1 发送一条完整命令。
2. 固件解析命令并分发到对应任务或模块。
3. 固件通过 UART1 返回结果。

这里的“完整命令”特指：大写、无空格、并以 `\r\n` 结束的命令帧。

### 3.2 成功回包

控制类命令成功时返回：

```text
OK
```

对电机、LED、NMOS、电源模式和两种旁路控制，`OK` 表示命令已被接收处理；它不保证物理输出或电机状态已完成切换。需要确认时，使用 `AT+MOTOR?` 或 `AT+OUTPUT?` 查询实际应用状态；产品级确认规则见主文档。

查询类命令成功时通常返回两行：

```text
+SENSE:...
OK
```

```text
+FAULT:...
OK
```

```text
+MOTOR:...
OK
```

### 3.3 失败回包

失败时返回：

```text
ERROR
```

或带原因码：

```text
ERROR:PARSE
ERROR:SENSE_NOT_READY
ERROR:LINE_TOO_LONG
ERROR:STATE
ERROR:STATE_BUSY
ERROR:OVER_TEMPERATURE
ERROR:LED_QUEUE
ERROR:MOTOR_QUEUE
ERROR:OUTPUT_QUEUE
ERROR:HEX
ERROR:UART_DISABLED
ERROR:UART_TX
ERROR:MOTOR_RUNNING
ERROR:FLASH_WRITE
ERROR:BAD_COMMAND
ERROR:STALL_CONFIG
ERROR:OUTPUT_FORMAT
ERROR:RESPONSE_TOO_LONG
ERROR:UNSUPPORTED
```

错误码定义与主机处理：

| 错误码 | 定义 | 主机处理 |
| --- | --- | --- |
| `PARSE` | 一般命令仅在收到完整 CRLF 帧、总长度小于 96 字节、正文不含空格/制表符/嵌入换行和小写字母、且以精确大写 `AT+` 开头并被识别为候选 AT 控制帧后，语法或参数仍不匹配时返回。缺少 CRLF 时尚未形成一帧，不会立即返回；第 128 个行字节返回 `LINE_TOO_LONG`；未通过候选筛选的完整行进入兼容透传路径（是否产生下游数据取决于桥接状态）。精确前缀 `AT+UARTTX=` 走专门分支，负载不合法返回 `HEX`，不是 `PARSE`。 | 本地校验命令、大小写、参数和 CRLF；不要依赖错误回包发现格式错误。 |
| `SENSE_NOT_READY` | 尚无可发布的传感快照。 | 稍后重新查询；不要把它当作状态改变。 |
| `LINE_TOO_LONG` | 接收行超过协议缓存上限。 | 按第 2.3.1 节停止新请求、排空残余响应与异步事件，并重新握手和状态同步；简单重发不安全。 |
| `STATE` | 命令的协议前置状态不成立。 | 用 `MOTOR?` 或 `OUTPUT?` 查询，再按产品流程决定下一步。 |
| `STATE_BUSY` | 当前状态快照或配置资源不可用。 | 延后并重新查询/请求，不假设命令已执行。 |
| `OVER_TEMPERATURE` | 温度保护拒绝危险开启：`CHARGE=ON`、`DRIVE=ON`、`NMOS1=ON`、`NMOS2=ON`、`MOTOR_BYPASS=ON`、`CHARGE_BYPASS=ON`，以及 `MOTOR=WAKE/FWD/REV/BRAKE/STOP`（`MOTOR=SLEEP` 不受此门控）。`LED=ON` 不属于温度门控。 | 保持安全状态并按主文档处理；不要仅凭单个 `SENSE?` 判定可恢复。 |
| `LED_QUEUE` | LED 请求未能入队。 | 查询或延后重试。 |
| `MOTOR_QUEUE` | 电机请求未能入队。 | 查询 `MOTOR?` 后决定是否重试。 |
| `OUTPUT_QUEUE` | 输出或电源路径请求未能入队。 | 查询 `OUTPUT?` 后决定是否重试。 |
| `HEX` | `UARTTX` 负载为空、非大写十六进制、长度为奇数或超过 32 字节。 | 修正并分块重发。 |
| `UART_DISABLED` | 两个下游 UART 均未启用。 | 先打开至少一路桥接。 |
| `UART_TX` | 至少一个已启用下游 UART 的发送失败。 | 查询桥接/下游链路后重试；结果不应视为已送达。 |
| `MOTOR_RUNNING` | 运行中的电机禁止修改堵转阈值。 | 停止电机后重新设置。 |
| `FLASH_WRITE` | 堵转阈值持久化写入失败。 | 重新查询当前值；避免无条件重复擦写。 |
| `BAD_COMMAND` | 内部命令对象无效，正常串口输入不应到达。 | 记录原始帧与版本，转入维护处理。 |
| `STALL_CONFIG` | 堵转配置服务返回未知内部状态。 | 查询当前值并转入维护处理。 |
| `OUTPUT_FORMAT` | `OUTPUT?` 响应格式化失败。 | 记录版本和响应，稍后重试。 |
| `RESPONSE_TOO_LONG` | `SENSE?` 响应超过内部发送缓冲。 | 记录版本和响应，稍后重试。 |
| `UNSUPPORTED` | 命令已解析但该类型当前未实现。 | 不要重试该类型；按版本能力处理。 |

## 4. 指令总览

### 4.1 控制类命令

| 分类 | 指令 | 作用 | 成功回包 |
| --- | --- | --- | --- |
| 透传 | AT+UART2=ON\r\n | 打开 UART2 透传 | OK |
| 透传 | AT+UART2=OFF\r\n | 关闭 UART2 透传 | OK |
| 透传 | AT+UART3=ON\r\n | 打开 UART3 透传 | OK |
| 透传 | AT+UART3=OFF\r\n | 关闭 UART3 透传 | OK |
| 透传 | AT+UART2&3=ON\r\n | 同时打开 UART2 和 UART3 透传 | OK |
| 透传 | AT+UART2&3=OFF\r\n | 同时关闭 UART2 和 UART3 透传 | OK |
| 透传 | AT+UARTTX=&lt;HEX&gt;\r\n | 向所有已打开目标发送 1～32 字节原始数据 | OK |
| LED | AT+LED=ON\r\n | 打开 LED 联动显示 | OK |
| LED | AT+LED=OFF\r\n | 关闭 LED 联动显示 | OK |
| 电机 | AT+MOTOR=SLEEP\r\n | 电机驱动进入睡眠 | OK |
| 电机 | AT+MOTOR=WAKE\r\n | 唤醒电机驱动但不输出 | OK |
| 电机 | AT+MOTOR=FWD\r\n | 电机正转 | OK |
| 电机 | AT+MOTOR=REV\r\n | 电机反转 | OK |
| 电机 | AT+MOTOR=BRAKE\r\n | 电机制动 | OK |
| 电机 | AT+MOTOR=STOP\r\n | 电机停止，当前实现等同制动 | OK |
| 电机限流 | AT+MOTOR_BYPASS=ON\r\n | 仅在 FWD/REV 时开启电机限流旁路 | OK |
| 电机限流 | AT+MOTOR_BYPASS=OFF\r\n | 关闭电机限流旁路 | OK |
| NMOS | AT+NMOS1=ON\r\n | 仅在 DRIVE 模式打开 NMOS1 | OK |
| NMOS | AT+NMOS1=OFF\r\n | 关闭 NMOS1 | OK |
| NMOS | AT+NMOS2=ON\r\n | 仅在 DRIVE 模式打开 NMOS2 | OK |
| NMOS | AT+NMOS2=OFF\r\n | 关闭 NMOS2 | OK |
| 电源路径 | AT+CHARGE=ON\r\n | 请求 CHARGE 模式的 60 秒循环；默认 ON 为 10 秒 | OK |
| 电源路径 | AT+CHARGE=OFF\r\n | 请求 OFF 模式 | OK |
| 充电限流 | AT+CHARGE_BYPASS=ON\r\n | 仅在请求模式为 CHARGE 时开启充电限流旁路 | OK |
| 充电限流 | AT+CHARGE_BYPASS=OFF\r\n | 关闭充电限流旁路 | OK |
| 电源路径 | AT+DRIVE=ON\r\n | 请求 DRIVE 模式 | OK |
| 电源路径 | AT+DRIVE=OFF\r\n | 请求 OFF 模式 | OK |
| 电源路径 | AT+POWER=OFF\r\n | 请求 OFF 模式 | OK |
| 充电配置 | AT+CHARGE_TIME=&lt;n&gt;\r\n | 设置每个周期的 ON 秒数，`n` 为 1～60 | OK |
| 电机保护 | AT+STALL_CURRENT=&lt;mA&gt;\r\n | 设置并持久化堵转阈值，`mA` 为 1000～30000；仅限非 FWD/REV | OK |

### 4.2 查询类命令

| 分类 | 指令 | 作用 |
| --- | --- | --- |
| 采样 | AT+SENSE?\r\n | 查询最近一次已发布的传感快照；它可能不是当前瞬时状态 |
| 故障 | AT+FAULT?\r\n | 查询 nFAULT 和 nFLT 引脚状态 |
| 电机 | AT+MOTOR?\r\n | 查询当前电机模式、电流与故障标志 |
| 输出 | AT+OUTPUT?\r\n | 查询请求电源模式、充电实际相位和全部输出应用状态 |
| 诊断 | AT+DIAG?\r\n | 查询控制链路、TX、传感任务及 UART2/UART3 RX 计数器 |
| 充电配置 | AT+CHARGE_TIME=?\r\n | 查询 RAM 内当前配置；返回 `+CHARGE_TIME:<n>` 后跟 `OK` |
| 电机保护 | AT+STALL_CURRENT=?\r\n | 查询当前堵转阈值；返回 `+STALL_CURRENT:<mA>` 后跟 `OK` |
| 版本 | AT+VERSION?\r\n | 查询固件版本号，用于握手 |

### 4.3 兼容写法

当前解析器仍然接受一条兼容写法，但同样要求大写、无空格并以 `\r\n` 结束：

```text
AT+UART23=ON\r\n
AT+UART23=OFF\r\n
```

它与下面的标准写法等效：

```text
AT+UART2&3=ON\r\n
AT+UART2&3=OFF\r\n
```

如果你在写新的上位机程序，建议优先使用标准写法 AT+UART2&3。

## 5. 各指令详细说明

### 5.1 透传控制命令

#### 5.1.1 打开 UART2 透传

请求：

```text
AT+UART2=ON
```

响应：

```text
OK
```

效果：

- UART1 上收到的非 AT 数据会被转发到 UART2。
- 不影响 UART1 后续继续接收新的 AT 命令。

#### 5.1.2 打开 UART3 透传

请求：

```text
AT+UART3=ON
```

响应：

```text
OK
```

#### 5.1.3 同时打开 UART2 和 UART3 透传

请求：

```text
AT+UART2&3=ON
```

响应：

```text
OK
```

效果：

- 非 AT 数据会同时发往 UART2 和 UART3。

#### 5.1.4 透传关闭示例

```text
AT+UART2=OFF
AT+UART3=OFF
AT+UART2&3=OFF
```

#### 5.1.5 二进制安全发送

请求：

```text
AT+UARTTX=00FF0D0A
```

负载规则：

- `<HEX>` 必须使用大写 `0-9A-F`。
- 长度必须为偶数，最少 2 个、最多 64 个十六进制字符，即 1～32 字节。
- 不允许空格、逗号、`0x` 前缀或其他分隔符。
- 上位机负载超过 32 字节时，必须自行分块，并在上一块收到 `OK` 后再发下一块。

发送目标由桥接开关决定：

- 只打开 UART2：发送到 UART2。
- 只打开 UART3：发送到 UART3。
- UART2、UART3 都打开：同一份原始字节依次发送到两路。
- 两路都关闭：返回 `ERROR:UART_DISABLED`。

固件只有在所有已选择目标都返回 `HAL_OK` 后才回复：

```text
OK
```

格式错误或发送失败分别返回：

```text
ERROR:HEX
ERROR:UART_TX
```

Hex 模式必须按原始字节语义处理。例如 `AT+UARTTX=00FF0D0A` 向目标发送的恰好是 `00 FF 0D 0A`，固件不会额外追加 CRLF。

### 5.2 LED 控制命令

#### 5.2.1 打开 LED 联动显示

请求：

```text
AT+LED=ON
```

响应：

```text
OK
```

说明：`ON` 仅在 `POWER` 请求模式为 `DRIVE` 时接受；否则返回 `ERROR:STATE`，共享状态不可读时返回 `ERROR:STATE_BUSY`。`OFF` 始终可请求。LED 不受温度门控，但仍受上述 DRIVE 状态门控。以 `OK` 后的 `AT+OUTPUT?` 确认，`OUTPUT.LIGHTS` 是 LED 主开关的回读字段。

#### 5.2.2 关闭 LED 联动显示

请求：

```text
AT+LED=OFF
```

响应：

```text
OK
```

### 5.3 电机控制命令

#### 5.3.1 睡眠

```text
AT+MOTOR=SLEEP
```

说明：请求电机进入 `SLEEP` 模式。

#### 5.3.2 唤醒

```text
AT+MOTOR=WAKE
```

说明：请求 `WAKE` 模式，不请求方向输出。

#### 5.3.3 正转

```text
AT+MOTOR=FWD
```

说明：请求 `FWD` 模式；完成状态以 `AT+MOTOR?` 为准。

#### 5.3.4 反转

```text
AT+MOTOR=REV
```

请求 `REV` 模式；完成状态以 `AT+MOTOR?` 为准。

#### 5.3.5 制动与停止

```text
AT+MOTOR=BRAKE
AT+MOTOR=STOP
```

`STOP` 与 `BRAKE` 的物理 GPIO 输出可相同，但请求模式会保留；`AT+MOTOR?` 分别返回 `MODE=STOP` 或 `MODE=BRAKE`，主机不得把它们视为同一个协议状态。

#### 5.3.6 电机启动限流电阻旁路

```text
AT+MOTOR_BYPASS=ON
AT+MOTOR_BYPASS=OFF
```

- `ON` 仅在当前电机模式为 `FWD` 或 `REV` 时接受；否则返回 `ERROR:STATE`。状态不可读取时返回 `ERROR:STATE_BUSY`。
- `OFF` 可随时请求。实际应用状态由 `AT+OUTPUT?` 确认。

### 5.4 NMOS 与互锁电源路径命令

#### 5.4.1 NMOS1 和 NMOS2

```text
AT+NMOS1=ON
AT+NMOS1=OFF
AT+NMOS2=ON
AT+NMOS2=OFF
```

说明：`ON` 仅在请求电源模式为 `DRIVE` 时接受；否则返回 `ERROR:STATE`。`OFF` 可请求关闭，实际状态由 `AT+OUTPUT?` 确认。

#### 5.4.2 充电、驱动与全关

```text
AT+CHARGE=ON
AT+CHARGE=OFF
AT+DRIVE=ON
AT+DRIVE=OFF
AT+POWER=OFF
```

说明：支持的请求模式为 `OFF`、`CHARGE` 与 `DRIVE`。`CHARGE=ON` 使用固定 60 秒周期，ON 时长由 `CHARGE_TIME` 决定，默认 10 秒；其余模式切换、请求与实际相位由 `AT+OUTPUT?` 返回的 `POWER` 和 `CHARGE_PHASE` 确认。旧的独立电源芯片命令不在当前命令集内。

#### 5.4.3 充电限流电阻旁路

```text
AT+CHARGE_BYPASS=ON
AT+CHARGE_BYPASS=OFF
```

- `ON` 仅在请求电源模式为 `CHARGE` 时接受；否则返回 `ERROR:STATE`。状态不可读取时返回 `ERROR:STATE_BUSY`。
- `OFF` 可随时请求。实际状态由 `AT+OUTPUT?` 确认。

#### 5.4.4 充电时间设置与查询

```text
AT+CHARGE_TIME=25
OK

AT+CHARGE_TIME=?
+CHARGE_TIME:25
OK
```

- `n` 只接受十进制整数 1～60；0、61、负数、小数、空值或额外字符返回 `ERROR:PARSE`。
- 周期固定为 60 秒，默认 10 秒 ON / 50 秒 OFF。配置只保存在 RAM，MCU 复位后恢复默认 10。
- 运行中设置新值会立即改变查询结果，但不修改当前相位和绝对截止时间；从下一次 ON 相位开始，整个新周期使用新配置。
- `n=60` 表示持续 ON。`CHARGE` 请求状态不等同于当前实际相位；用 `AT+OUTPUT?` 确认。

#### 5.4.5 温度保护的协议可见性

当前协议**没有**温度锁存状态查询字段。受保护的开启命令可能返回 `ERROR:OVER_TEMPERATURE`；关闭类命令、`AT+MOTOR=SLEEP`、查询和 `CHARGE_TIME` 设置/查询仍可发送。`AT+SENSE?` 是最近一次已发布的快照，可能早于当前保护状态，不能作为解除保护或重新开启的充分条件。产品级门控和恢复流程见主文档。

### 5.5 传感查询命令

#### 5.5.1 请求格式

```text
AT+SENSE?
```

#### 5.5.2 响应格式

```text
+SENSE:BATT_NTC=<value>,BATT_V=<value>V,MCU_C=<value>,LM51770_C=<value>,MP4317_C=<value>,DRV8874_C=<value>,CHARGE_MOS_C=<value>,MOTOR_I=<value>A,TICK=<value>,COUNT=<value>,STK_AT=<value>,STK_SENSOR=<value>,STK_MOTOR=<value>,TX_SP=<value>,TX_LS=<value>
OK
```

字段说明：

| 字段 | 词法契约 |
| --- | --- |
| BATT_NTC / MCU_C / LM51770_C / MP4317_C / DRV8874_C / CHARGE_MOS_C | `-?[0-9]+\.[0-9]C`（可为负，恰一位小数）或精确 `ERR`。 |
| BATT_V | 非负电压，格式为 `[0-9]+\.[0-9]+V`。正常语义为一位小数；格式化实现会四舍五入但不进位，边界时小数 token 可输出 `10`，故解析器必须接受它。此字段没有 `ERR` 输出分支。 |
| MOTOR_I | 非负电流，格式为 `[0-9]+\.[0-9]A`（恰一位小数）；此字段没有 `ERR` 输出分支。 |
| TICK / COUNT / STK_AT / STK_SENSOR / STK_MOTOR | 无符号十进制整数：`[0-9]+`。 |
| TX_SP | 最近一次发送前的 HAL UART `gState` 原始无符号十进制值：`[0-9]+`；当前 HAL 枚举定义的值为 0、32、33、34、35、36、160、224。 |
| TX_LS | 最近一次 `HAL_UART_Transmit` 返回值的无符号十进制：`0`=OK、`1`=ERROR、`2`=BUSY、`3`=TIMEOUT。 |

字段集合和顺序固定，整行等价于：

```text
+SENSE:BATT_NTC=<T>,BATT_V=<V>,MCU_C=<T>,LM51770_C=<T>,MP4317_C=<T>,DRV8874_C=<T>,CHARGE_MOS_C=<T>,MOTOR_I=<I>,TICK=<U>,COUNT=<U>,STK_AT=<U>,STK_SENSOR=<U>,STK_MOTOR=<U>,TX_SP=<SP>,TX_LS=<LS>\r\nOK\r\n
```

其中 `T`、`V`、`I`、`U`、`SP`、`LS` 分别采用上表定义。示例（同时覆盖负温度、`ERR` 与计数器）：

```text
+SENSE:BATT_NTC=-5.2C,BATT_V=37.0V,MCU_C=ERR,LM51770_C=25.0C,MP4317_C=24.8C,DRV8874_C=26.1C,CHARGE_MOS_C=23.5C,MOTOR_I=0.8A,TICK=123,COUNT=7,STK_AT=0,STK_SENSOR=0,STK_MOTOR=0,TX_SP=32,TX_LS=0
OK
```

`SENSE` 是已发布的采样快照，可能不是当前瞬时状态；上电初期尚无快照时返回 `ERROR:SENSE_NOT_READY`。它不提供温度锁存查询，且单个快照不能作为产品保护解除的充分依据。

### 5.6 故障查询命令

#### 5.6.1 请求格式

```text
AT+FAULT?
```

#### 5.6.2 响应格式

```text
+FAULT:DRV=<0|1>,AUX=<0|1>
OK
```

字段说明：

- DRV：驱动故障输入状态
- AUX：辅助故障输入状态

当前实现中：

- 引脚为低电平时，返回值为 1，表示故障有效
- 引脚为高电平时，返回值为 0，表示未检测到故障

### 5.7 电机状态查询命令

#### 5.7.1 请求格式

```text
AT+MOTOR?
```

#### 5.7.2 响应格式

```text
+MOTOR:MODE=<mode>,CURRENT_MA=<value>,OVERCURRENT=<0|1>,FAULT=<0|1>
OK
```

字段说明：

| 字段 | 含义 |
| --- | --- |
| MODE | 当前电机模式，可能是 SLEEP、WAKE、FWD、REV、BRAKE、STOP |
| CURRENT_MA | 电机监测任务最后一次写入的毫安整数读数。 |
| OVERCURRENT | 是否已锁存过流 |
| FAULT | 电机状态快照中最近一次写入的 `drv_fault_active` 值。 |

注意：

- `CURRENT_MA` 只在模式为 `FWD` 或 `REV` 时由 10 ms 监测周期尝试刷新；采样或状态读取失败时会保留最后一次值，因此它不是无条件实时测量。进入其他模式的模式请求会写入 0。
- 堵转触发后，固件写入 `MODE=BRAKE`、`OVERCURRENT=1`，并保留触发该保护的 `CURRENT_MA`；随后该值不作为连续实时电流更新。
- `FAULT` 在每次写入电机状态时从驱动 nFAULT 采样；非 `FWD`/`REV` 时任务不会持续刷新它，因此可能陈旧。实时 nFAULT/nFLT 引脚状态请使用 `AT+FAULT?`。
- 主机应组合 `MODE`、`OVERCURRENT`、`FAULT` 与 `CURRENT_MA` 判断电机状态，不得把任一快照字段单独当作当前运行或实时故障证明。`MOTOR_I` 是独立的 SENSE 快照字段。

### 5.7.3 堵转阈值设置与查询

设置请求（单位 mA，只接受 1000～30000 的无符号十进制整数）：

```text
AT+STALL_CURRENT=4000
```

成功响应：

```text
OK
```

查询请求与响应：

```text
AT+STALL_CURRENT=?
+STALL_CURRENT:4000
OK
```

语义与限制：

- 默认值为 4000 mA，设置成功后持久化保存。
- FWD/REV 时设置返回 `ERROR:MOTOR_RUNNING`；查询始终允许。设置相同值直接返回 `OK`。
- 超范围、负数、小数、带单位或其他格式返回 `ERROR:PARSE`；写入失败返回 `ERROR:FLASH_WRITE`；状态不可用返回 `ERROR:STATE_BUSY`。

### 5.8 输出状态查询命令

请求与固定响应格式：

```text
AT+OUTPUT?
+OUTPUT:POWER=<OFF|CHARGE|DRIVE>,CHARGE_PHASE=<IDLE|ON|OFF>,NMOS1=<0|1>,NMOS2=<0|1>,LIGHTS=<0|1>,MOTOR_BYPASS=<0|1>,CHARGE_BYPASS=<0|1>
OK
```

`POWER` 表示请求电源模式，`CHARGE_PHASE` 表示实际充电相位。其余字段均为固件已应用到 GPIO 或 LED 主开关的确认状态；字段集合与顺序固定。

### 5.9 诊断查询命令

#### 5.9.1 请求格式

```text
AT+DIAG?
```

#### 5.9.2 响应格式

```text
+DIAG:RX_ISR=<n>,RX_BYTE=<n>,RX_OVERFLOW=<n>,RX_ERR=<n>,ORE=<n>,NE=<n>,FE=<n>,PE=<n>,LINE_TOO_LONG=<n>,AT_LOOP=<n>,TX_CALL=<n>,TX_OK=<n>,TX_TIMEOUT=<n>,TX_ERR=<n>,TX_BUSY=<n>,TX_STATE_PRE=<n>,TX_STATE_POST=<n>,TX_ERR_PRE=<n>,TX_ERR_POST=<n>,TX_LAST_STATUS=<n>,SENSOR_LOOP=<n>,SENSOR_PUBLISH=<n>,SENSOR_LAST_PUBLISH_TICK=<n>,SENSOR_ADC1_READ_FAIL=<n>,SENSOR_ADC2_READ_FAIL=<n>,UART2_RX_BYTE=<n>,UART2_RX_OVERFLOW=<n>,UART3_RX_BYTE=<n>,UART3_RX_OVERFLOW=<n>
OK
```

字段说明：

| 字段 | 含义 |
| --- | --- |
| RX_ISR | UART1 接收中断计数。 |
| RX_BYTE | UART1 接收字节计数。 |
| RX_OVERFLOW | 环形缓冲满时丢字节次数 |
| RX_ERR / ORE / NE / FE / PE | UART1 接收错误分类计数。 |
| LINE_TOO_LONG | 接收行超长计数。 |
| AT_LOOP | AT 控制循环计数。 |
| TX_CALL / TX_OK / TX_TIMEOUT / TX_ERR / TX_BUSY | 发送调用与结果计数。 |
| TX_STATE_PRE / TX_STATE_POST / TX_ERR_PRE / TX_ERR_POST / TX_LAST_STATUS | 最近一次发送的诊断状态。 |
| SENSOR_LOOP / SENSOR_PUBLISH / SENSOR_LAST_PUBLISH_TICK | 传感采样与发布计数。 |
| SENSOR_ADC1_READ_FAIL / SENSOR_ADC2_READ_FAIL | 传感采集失败计数。 |
| UART2_RX_BYTE / UART3_RX_BYTE | 下游 UART 接收字节计数。 |
| UART2_RX_OVERFLOW / UART3_RX_OVERFLOW | 下游 UART 接收溢出计数。 |

用途：

- 用于排查 “命令发出但收不到回包” 类问题。
- 所有诊断计数器均自本次 MCU 启动起累计；查询不会清零，MCU 复位会清零。它们是无符号计数器，长期运行可能回绕。
- 这些字段是只读观测，不参与自动恢复。

### 5.10 版本查询命令（握手）

#### 5.10.1 请求格式

```text
AT+VERSION?
```

#### 5.10.2 响应格式

```text
+VERSION:<version>
OK
```

示例：

```text
+VERSION:release-v3.3
OK
```

说明：

- 用于上位机连接 UART1 后的握手；当前上位机只接受精确的 `+VERSION:release-v3.3` 后跟 `OK`。
- 建议在 500 ms 内识别终止响应；超时不表示命令必然未执行。
- 当前版本使用 `AT+CHARGE`、`AT+DRIVE` 和 `AT+POWER=OFF`；`CHARGE=ON` 表示间歇循环请求已启用，不表示当前处于 ON 相位。
- 当前版本严格解析语义化 SENSE 字段，不兼容旧的编号 NTC 字段。
- 历史演进：`AT+UARTTX`、`+UART2RX`、`+UART3RX` 和 UART2/UART3 接收诊断字段在旧版本引入，并在当前版本保留。版本号用于能力判断；旧版本上位机不能假定支持当前的二进制隧道或电源路径命令集。

## 6. 双向隧道与兼容透传规则

### 6.1 推荐路径：AT 十六进制帧

当前版本上位机应使用 `AT+UARTTX=<HEX>` 发送数据。该路径具有以下性质：

- 二进制安全，包括 `0x00`、CR、LF 和 `0xFF`。
- 不依赖负载本身携带行结束符。
- 每块都有明确的 `OK` 或 `ERROR:<reason>`。
- 可同时向 UART2 和 UART3 发送。

### 6.2 UART2/UART3 异步接收事件

UART2 或 UART3 收到原始数据后，固件会发出：

```text
+UART2RX:<HEX>\r\n
+UART3RX:<HEX>\r\n
```

每个事件携带 1～32 字节。这个范围只是桥接任务一次排空的调度分块，不是下游消息或帧边界。事件示例：

```text
+UART2RX:000D0AFF\r\n
+UART3RX:414243\r\n
```

处理约束：

- 事件可能出现在等待普通 AT 命令响应期间。
- 事件是带来源信息的旁路数据，不是当前命令的数据行，也不结束当前请求。
- 上位机必须先按 `UART2RX` / `UART3RX` 路由事件，解码 HEX 后按端口拼接为字节流，再由下游协议自行重组消息或帧；不得把一个事件当作下游消息边界。
- 同一端口只按收到的事件顺序拼接；UART2 与 UART3 事件之间没有可依赖的全局顺序。
- 事件只包含十六进制负载，不跟随 `OK`。
- 上报到 UART1 是尽力发送：发送失败时固件不重发，也没有事件序号或丢失通知。结合 `UART2_RX_OVERFLOW` / `UART3_RX_OVERFLOW` 与下游协议的完整性机制处理缺口。

### 6.3 兼容路径：CRLF 文本行

历史兼容路径：UART1 上不被识别为 AT 命令的完整 CRLF 文本行仍会被转发到已打开的 UART2/UART3。例如：

```text
HELLO\r\n
{"cmd":"ping"}\r\n
```

该兼容路径仅推荐旧系统使用：

- 依赖 CRLF 才能形成完整行。
- 发送函数按字符串长度处理，不适合含 `0x00` 的数据。
- 只覆盖 UART1 到下游的单向文本转发；反向数据仍通过本节异步事件返回。
- 以 `AT+` 开头且被识别为控制命令的数据不会转发。

### 6.4 桥接开关与缓存

- UART2 未打开时，不向 UART2 发送，也不向上位机发布 UART2 接收事件。
- UART3 未打开时，不向 UART3 发送，也不向上位机发布 UART3 接收事件。
- 关闭某一路时会清空该路接收环形缓冲，防止旧数据在下一次打开后出现。
- 两路都关闭时发送 `AT+UARTTX` 会返回 `ERROR:UART_DISABLED`。

## 7. 推荐联调顺序

本节仅覆盖协议层烟雾测试；CHARGE、DRIVE、电机与安全退出的完整流程见[集成与使用说明](./stem-hub模块集成与使用说明.md)。

1. 发送 `AT+VERSION?`，验证精确的 `+VERSION:release-v3.3` 与终止 `OK`。
2. 发送 `AT+OUTPUT?`，验证固定字段集合与顺序。
3. 发送一个安全关闭命令 `AT+LED=OFF`，再发送 `AT+OUTPUT?`，验证请求与状态查询闭环。
4. 打开 UART2 或 UART3，向下游注入数据，验证 `+UART2RX:` 或 `+UART3RX:` 异步事件被正确分流，且不会结束在途请求。

最小示例（每行在线上均以 CRLF 结束）：

```text
AT+VERSION?
AT+OUTPUT?
AT+LED=OFF
AT+OUTPUT?
AT+UART2=ON
```

## 8. 上位机实现建议

上位机实现仅应承担协议层职责：

- 串行化请求；每次只保留一个在途 AT 请求。
- 按 CRLF 解析接收数据：`OK`、`ERROR` 或 `ERROR:<reason>` 是该请求的终止响应；查询数据行在其前出现。
- 优先分流 `+UART2RX:` / `+UART3RX:`，这些异步事件不属于在途请求，也不终止请求。
- 对超过 32 字节的 `UARTTX` 负载分块，并在上一块得到终止响应后再发送下一块。
- 设置命令超时，但超时**不表示未执行**；恢复通信后使用查询命令重新建立事实状态。

UI 门控、产品状态机和保护恢复属于产品层，见[集成与使用说明](./stem-hub模块集成与使用说明.md)。

## 9. 常见问题

### 9.1 为什么发送了命令但没回包

优先检查：

- 是否发到了 UART1，而不是 UART2/UART3
- 串口参数是否是 9600 8N1
- 是否带了行结束符
- 是否命令字使用了小写

### 9.2 为什么查询采样值不稳定

`SENSE` 是最近一次已发布快照，不是实时控制状态。不要用单个 `SENSE` 响应判断保护是否已经解除；使用主文档规定的产品恢复流程。

### 9.3 为什么 CURRENT_MA 看起来不像实时电流

`CURRENT_MA` 是 `MOTOR?` 的查询字段；`MOTOR_I` 属于 SENSE 快照。两者的采样时点不同，不能互相替代。

### 9.4 为什么透传数据丢了

优先检查：

- 对应桥接是否已经打开
- `AT+UARTTX` 是否使用大写、偶数长度且不超过 64 个 Hex 字符
- 上位机是否等待上一帧 `OK` 后再发送下一帧
- `UART2_RX_OVERFLOW` / `UART3_RX_OVERFLOW` 是否增长
- 异步事件是否被错误归入普通 AT 命令响应

## 10. 相关文件

- [README.md](README.md)：仓库总说明
- [App/Src/app_at_protocol.c](App/Src/app_at_protocol.c)：AT 解析实现
- [App/Src/app_at_task.c](App/Src/app_at_task.c)：AT 命令执行和回包实现
- [App/Src/app_bridge_task.c](App/Src/app_bridge_task.c)：UART2/UART3 接收缓冲排空和事件上报
- [App/Src/app_uart_tunnel.c](App/Src/app_uart_tunnel.c)：异步接收事件编码
- [tests/test_at_protocol.c](tests/test_at_protocol.c)：AT 解析测试
- [tests/test_uart_tunnel.c](tests/test_uart_tunnel.c)：隧道事件编码测试
