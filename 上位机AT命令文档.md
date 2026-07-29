# 上位机 AT 命令文档

本文档面向上位机开发、联调和测试人员，说明当前固件支持的 AT 指令、串口收发约定、回包格式、透传规则和联调建议。

> 适用固件：`release-v3.0`
> 更新方式：在 v2.2 文档基础上更新电源路径协议和传感 ADC 滤波语义。

## 1. 文档范围

本文档描述的是当前仓库内已经实现的 AT 控制面，不包含未来规划中的功能。

当前已覆盖：

- UART1 AT 命令收发
- UART2 和 UART3 透传开关控制
- 通过 `AT+UARTTX=<HEX>` 向 UART2/UART3 精确发送二进制数据
- UART2/UART3 接收数据通过异步十六进制事件回传
- LED 总开关控制
- 电机模式控制与状态查询
- NMOS1、NMOS2 控制
- MCU 强制互锁的充电路径（LM51770）与驱动路径（MP4317）控制
- 传感采样结果查询
- nFAULT、nFLT 状态查询

当前未覆盖：

- 故障自动恢复
- 参数持久化保存
- AT 命令权限管理
- AT 命令版本协商

## 2. 串口通信约定

### 2.1 串口参数

UART1、UART2、UART3 当前都使用相同配置：

| 参数 | 值 |
| --- | --- |
| 波特率 | 115200 |
| 数据位 | 8 |
| 校验位 | None |
| 停止位 | 1 |
| 流控 | None |

### 2.2 AT 命令入口

- UART1 是唯一的 AT 命令入口。
- UART2 和 UART3 不解析 AT；它们是受 UART1 控制的下游收发通道。
- UART2/UART3 的接收字节不会在中断中直接发送，而是先进入环形缓冲，再由 `bridgeTask` 封装回 UART1。

### 2.3 行结束符

上位机必须使用 CRLF，也就是 `\r\n`，作为一条 AT 命令的结束符。例如：

```text
AT+LED=ON\r\n
```

当前版本不接受下面这些情况：

- 没有 `\r\n` 结尾
- 只有 `\r` 或只有 `\n`
- 命令中夹带空格或制表符

例如，下面这些写法都会被拒绝：

```text
AT+LED=ON
AT + LED = ON
AT+LED = ON\r\n
AT+LED=ON\n
```

### 2.4 大小写规则

当前解析器是严格大小写敏感的，上位机必须发送大写命令字和值：

```text
AT+MOTOR=FWD\r\n
```

下面这些当前都会被拒绝：

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
ERROR:STATE_BUSY
ERROR:LED_QUEUE
ERROR:MOTOR_QUEUE
ERROR:OUTPUT_QUEUE
ERROR:HEX
ERROR:UART_DISABLED
ERROR:UART_TX
ERROR:UNSUPPORTED
```

说明：

- ERROR:PARSE：格式合法但内容不匹配当前支持的命令集
- ERROR:SENSE_NOT_READY：传感任务尚未完成第一次有效采样
- ERROR:LINE_TOO_LONG：一行数据超出内部缓存限制
- ERROR:STATE_BUSY：读取状态时互斥资源暂不可用
- ERROR:LED_QUEUE：LED 控制消息入队失败
- ERROR:MOTOR_QUEUE：电机控制消息入队失败
- ERROR:OUTPUT_QUEUE：NMOS 或完整电源模式控制消息入队失败
- ERROR:PARSE：命令帧属于 AT 格式但不匹配当前命令集；旧的独立 LM51770/MP4317 指令也返回此错误
- ERROR:HEX：`AT+UARTTX` 的十六进制负载为空、长度非法、含非法字符或超过 32 字节
- ERROR:UART_DISABLED：UART2 和 UART3 均未打开，无法执行 `AT+UARTTX`
- ERROR:UART_TX：至少一个已选择目标的 HAL 串口发送失败
- ERROR:UNSUPPORTED：解析成功但当前版本不支持该类型

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
| NMOS | AT+NMOS1=ON\r\n | 打开 NMOS1 | OK |
| NMOS | AT+NMOS1=OFF\r\n | 关闭 NMOS1 | OK |
| NMOS | AT+NMOS2=ON\r\n | 打开 NMOS2 | OK |
| NMOS | AT+NMOS2=OFF\r\n | 关闭 NMOS2 | OK |
| 电源路径 | AT+CHARGE=ON\r\n | 先关闭两路，再仅打开 LM51770 | OK |
| 电源路径 | AT+CHARGE=OFF\r\n | 同时关闭 LM51770 与 MP4317 | OK |
| 电源路径 | AT+DRIVE=ON\r\n | 先关闭两路，再仅打开 MP4317 | OK |
| 电源路径 | AT+DRIVE=OFF\r\n | 同时关闭 LM51770 与 MP4317 | OK |
| 电源路径 | AT+POWER=OFF\r\n | 同时关闭 LM51770 与 MP4317 | OK |

### 4.2 查询类命令

| 分类 | 指令 | 作用 |
| --- | --- | --- |
| 采样 | AT+SENSE?\r\n | 查询最近传感快照；五路传感 ADC 为最近五周期均值 |
| 故障 | AT+FAULT?\r\n | 查询 nFAULT 和 nFLT 引脚状态 |
| 电机 | AT+MOTOR?\r\n | 查询当前电机模式、电流与故障标志 |
| 诊断 | AT+DIAG?\r\n | 查询控制链路、TX、传感任务及 UART2/UART3 RX 计数器 |
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

说明：

- LED1 为上电指示灯，启动后 LED1 与 LED3 交替闪烁 5 秒（自检序列），之后熄灭进入正常工作状态。
- LED1 和 LED3 与电机方向联动（LED1 = 前进，LED3 = 后退）。原 LED2 (PA8) 已取消。
- 这里的 ON/OFF 影响的是 LED1 和 LED3 的联动显示使能。

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

说明：

- nSLEEP 被拉低。
- 电机驱动进入休眠状态。

#### 5.3.2 唤醒

```text
AT+MOTOR=WAKE
```

说明：

- 仅唤醒驱动，不立即使能输出。

#### 5.3.3 正转

```text
AT+MOTOR=FWD
```

说明：

- 若当前处于睡眠，会先 wake。
- 若当前是反转，会先制动并等待死区时间，再切换方向。

#### 5.3.4 反转

```text
AT+MOTOR=REV
```

逻辑与 FWD 类似。

#### 5.3.5 制动与停止

```text
AT+MOTOR=BRAKE
AT+MOTOR=STOP
```

当前版本中 STOP 与 BRAKE 的行为一致，都会让 EN 关闭并保持驱动唤醒。

### 5.4 NMOS 与互锁电源路径命令

#### 5.4.1 NMOS1 和 NMOS2

```text
AT+NMOS1=ON
AT+NMOS1=OFF
AT+NMOS2=ON
AT+NMOS2=OFF
```

说明：

- ON 表示对应 GPIO 输出高电平。
- OFF 表示对应 GPIO 输出低电平。

#### 5.4.2 充电、驱动与全关

```text
AT+CHARGE=ON
AT+CHARGE=OFF
AT+DRIVE=ON
AT+DRIVE=OFF
AT+POWER=OFF
```

说明：

- LM51770（PB3 EN/UVLO）与 MP4317（PA8）均为低电平使能。
- 固件只允许三种稳定状态：两路全关、仅 LM51770 开、仅 MP4317 开。
- `AT+CHARGE=ON` 先把 PB3、PA8 都置为关断电平，再仅拉低 PB3。
- `AT+DRIVE=ON` 先把 PB3、PA8 都置为关断电平，再仅拉低 PA8。
- `AT+CHARGE=OFF`、`AT+DRIVE=OFF`、`AT+POWER=OFF` 都关闭两路。
- 旧的 `AT+LM51770=ON/OFF` 与 `AT+MP4317=ON/OFF` 已删除，不能绕过 MCU 互锁。

### 5.5 传感查询命令

#### 5.5.1 请求格式

```text
AT+SENSE?
```

#### 5.5.2 响应格式

```text
+SENSE:BATT_NTC=<value>,BATT_V=<value>V,NTC1_C=<value>C,NTC2_C=<value>C,NTC3_C=<value>C,MOTOR_I=<value>A,TICK=<value>,COUNT=<value>,STK_AT=<value>,STK_SENSOR=<value>,STK_MOTOR=<value>,TX_SP=<value>,TX_LS=<value>
OK
```

字段说明：

| 字段 | 含义 |
| --- | --- |
| BATT_NTC | 电池 NTC 温度（℃），保留 1 位小数（如 25.1C、-5.2C、ERR）。查表法实现，对应 ADC1 IN4。 |
| BATT_V | 电池电压当前值，单位为伏特（V），保留 1 位小数（如 3.3V、37.0V）。已考虑 100kΩ + 5kΩ 电阻分压（实际倍率 21）。 |
| NTC1_C | NTC1 温度（℃），保留 1 位小数（如 25.3C、-5.2C），查表法实现 |
| NTC2_C | NTC2 温度（℃），格式同 NTC1_C |
| NTC3_C | NTC3 温度（℃），格式同 NTC1_C |
| MOTOR_I | DRV8874 IPROPI 电流，保留 1 位小数（如 0.8A、2.9A）。0.1 A 分辨率，最高 2.9 A（ADC 物理满量程；电机不在 FWD/REV 时固定为 0.0A）。 |
| TICK | 本次样本写入时的系统 Tick |
| STK_AT / STK_SENSOR / STK_MOTOR | atTask / sensorTask / motorTask 栈高水位（word），被动观测字段 |
| TX_SP / TX_LS | 最近一次发送前的 `huart->gState` 与返回值（HAL 状态机快照），被动观测字段 |
| COUNT | 样本计数 |

NTC 拓扑为 3V3 -- NTC -- ADC测点 -- 470Ω -- GND。

BATT_NTC（电池 NTC，ADC1 IN4）使用 3435K 系 NTC，电阻分压比相同但型号与 NTC1_C/NTC2_C/NTC3_C 不同：  
- BATT_NTC 范围 -55°C ~ +125°C，钳位到表外时分别返回 -55.0C / 125.0C。读数为 0（开路）时钳位到 -55.0C，读数 ≥ 3300mV（短路）时返回 ERR。  
- NTC1/NTC2/NTC3 使用 HNTC0603-103F3450FA，范围 -40°C ~ +125°C，钳位到表外时分别返回 -40.0C / 125.0C。读数为 0（开路）时钳位到 -40.0C，读数 ≥ 3300mV（短路）时返回 ERR。

MOTOR_I 来自 DRV8874 IPROPI 镜像电流链路：  
- 拓扑：IPROPI -- [R19=2.5kΩ] -- GND → ADC2 IN8。  
- 公式：`I(A) = V_IPROPI(V) / (AIPROPI × R19) = V_IPROPI(V) / 1.125`（AIPROPI 取 450 µA/A）。  
- 寄存器输出 0.1 A 分辨率整数 (deci-A)：`motor_current_a_deci = round(mA / 100)`，上限钳到 29 (= 2.9 A)。  
- 仅当电机在 FWD/REV 时刷值；SLEEP/WAKE/BRAKE/STOP 一律为 0。  
- > 3 A 的强短路靠 DRV8874 自身的 IOCP 处理（datasheet IOCP = 6~10 A），IPROPI 路径在 2.93 A 处 ADC 饱和。

注意：

- BATT_NTC / NTC1_C / NTC2_C / NTC3_C 在 0~85°C 区间精度约 ±0.5°C，<0°C 因 ADC 量化误差较大（Vadc 已 < 25mV）。
- 只有五路 ADC 在同一采样周期全部读取成功时才同步推进窗口；BATT_NTC、BATT_V、NTC1_C、NTC2_C、NTC3_C 先对最近五个完整成功周期的原始值求均值，再换算物理量，启动前四个完整周期按已有样本数计算。
- 五个固定窗口和运行和位于静态 RAM，不在 sensorTask 栈上创建五份快照；单路运行和最大为 20475。
- MOTOR_I 由电机状态产生，sensorTask 每秒最多更新一次；上次残值不影响停机显示（电机模式 ≠ FWD/REV 时强制归零）。
- MOTOR_I 参与即时过流保护，不使用五周期滑动平均。
- 如果系统刚上电，第一次有效采样尚未完成，可能返回 ERROR:SENSE_NOT_READY。

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

- DRV：对应 DRV8874 的 nFAULT 读取结果
- AUX：对应另外一路 nFLT 输入读取结果

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
| CURRENT_MA | 当前电流读数 |
| OVERCURRENT | 是否已锁存过流 |
| FAULT | 当前是否检测到驱动故障 |

注意：

- CURRENT_MA 基于 IPROPI 镜像电流公式换算 (`I_mA = V_mV × 1000 / 1125`)，单位为毫安整数。Vref=3.3V 下 IPROPI 路径的最大量程 ≈ 2.93 A（= 2933 mA），更高读数受 ADC 饱和限制。需 0.1 A 分辨率或停机归零语义请用 `AT+SENSE?` 的 `MOTOR_I` 字段。
- 当电流读数超过当前阈值时，固件会转入制动并置位 OVERCURRENT。

### 5.8 诊断查询命令

#### 5.8.1 请求格式

```text
AT+DIAG?
```

#### 5.8.2 响应格式

```text
+DIAG:RX_ISR=<n>,RX_BYTE=<n>,RX_OVERFLOW=<n>,RX_ERR=<n>,ORE=<n>,NE=<n>,FE=<n>,PE=<n>,LINE_TOO_LONG=<n>,AT_LOOP=<n>,TX_CALL=<n>,TX_OK=<n>,TX_TIMEOUT=<n>,TX_ERR=<n>,TX_BUSY=<n>,TX_STATE_PRE=<n>,TX_STATE_POST=<n>,TX_ERR_PRE=<n>,TX_ERR_POST=<n>,TX_LAST_STATUS=<n>,SENSOR_LOOP=<n>,SENSOR_PUBLISH=<n>,SENSOR_LAST_PUBLISH_TICK=<n>,SENSOR_ADC1_READ_FAIL=<n>,SENSOR_ADC2_READ_FAIL=<n>,UART2_RX_BYTE=<n>,UART2_RX_OVERFLOW=<n>,UART3_RX_BYTE=<n>,UART3_RX_OVERFLOW=<n>
OK
```

字段说明：

| 字段 | 含义 |
| --- | --- |
| RX_ISR | UART1 IRQ 总触发次数（在中断入口直接自增） |
| RX_BYTE | RX ISR 进入 push 路径的总次数 |
| RX_OVERFLOW | 环形缓冲满时丢字节次数 |
| RX_ERR | HAL_UART_ErrorCallback 总次数 |
| ORE | 硬件 overrun 错误次数 |
| NE | 噪声错误次数 |
| FE | 帧错误次数 |
| PE | 奇偶校验错误次数（本固件未启用校验，正常为 0） |
| LINE_TOO_LONG | atTask 行缓冲溢出次数 |
| AT_LOOP | atTask 主循环 acquire 成功次数（任务还活着的证据） |
| TX_CALL | `App_RuntimeSendBytes` 调用次数，包括 AT 回包、异步事件和下游 UART 发送 |
| TX_OK | HAL_UART_Transmit 返回 HAL_OK 次数 |
| TX_TIMEOUT | HAL_UART_Transmit 返回 HAL_TIMEOUT 次数 |
| TX_ERR | HAL_UART_Transmit 返回 HAL_ERROR 次数 |
| TX_BUSY | HAL_UART_Transmit 返回 HAL_BUSY 次数 |
| TX_STATE_PRE / TX_STATE_POST | 最近一次发送前后的 HAL UART `gState` 原始值 |
| TX_ERR_PRE / TX_ERR_POST | 最近一次发送前后的 HAL UART `ErrorCode` 原始值 |
| TX_LAST_STATUS | 最近一次 HAL_UART_Transmit 返回值，0/1/2/3 分别对应 OK/ERROR/BUSY/TIMEOUT |
| SENSOR_LOOP | 传感任务循环计数 |
| SENSOR_PUBLISH | 有效传感快照发布次数 |
| SENSOR_LAST_PUBLISH_TICK | 最近一次发布传感快照的系统 tick |
| SENSOR_ADC1_READ_FAIL | ADC1 采集失败次数 |
| SENSOR_ADC2_READ_FAIL | ADC2 采集失败次数 |
| UART2_RX_BYTE / UART3_RX_BYTE | UART2/UART3 接收中断进入字节入队路径的总次数 |
| UART2_RX_OVERFLOW / UART3_RX_OVERFLOW | 对应接收环形缓冲已满时的丢字节次数 |

用途：

- 用于排查 “命令发出但收不到回包” 类问题。
- 计数器从 0 开始累计，不掉电不清零。
- 固件侧不阻塞、不重置这些计数器，仅在 `AT+DIAG?` 时拷快照输出。
- v2.2 不存在 `UART_WDG` 字段，也没有 UART 静默看门狗；诊断计数器只用于观测，不参与自动恢复。

### 5.9 版本查询命令（握手）

#### 5.9.1 请求格式

```text
AT+VERSION?
```

#### 5.9.2 响应格式

```text
+VERSION:<version>
OK
```

示例：

```text
+VERSION:release-v3.0
OK
```

说明：

- 用于上位机连接 UART1 后的握手。拿到 `+VERSION:...` + `OK` 即可确认固件能解析且能应答。
- `<version>` 在固件侧的 `app_config.h::APP_FIRMWARE_VERSION` 定义，bump 版本只需改这一行。
- 建议超时：500 ms 之内没拿到 `OK` 即视为握手失败。
- v3.0 使用 `AT+CHARGE`、`AT+DRIVE` 和 `AT+POWER=OFF`；旧上位机不能继续发送独立芯片开关命令。
- v2.2 相比 v2.1 新增的 `AT+UARTTX`、`+UART2RX`、`+UART3RX` 和四个 UART2/UART3 接收诊断字段在 v3.0 中继续保留。
- 版本号可用于命令集能力判断；v2.1 上位机不能假定固件支持双向二进制隧道，v2.2 上位机也不能假定独立电源芯片指令仍有效。

## 6. 双向隧道与兼容透传规则

### 6.1 推荐路径：AT 十六进制帧

v2.2 上位机应使用 `AT+UARTTX=<HEX>` 发送数据。该路径具有以下性质：

- 二进制安全，包括 `0x00`、CR、LF 和 `0xFF`。
- 不依赖负载本身携带行结束符。
- 每块都有明确的 `OK` 或 `ERROR:<reason>`。
- 可同时向 UART2 和 UART3 发送。

### 6.2 UART2/UART3 异步接收事件

UART2 或 UART3 收到原始数据后，固件会在任务上下文发出：

```text
+UART2RX:<HEX>\r\n
+UART3RX:<HEX>\r\n
```

每个事件携带 1～32 字节。事件示例：

```text
+UART2RX:000D0AFF\r\n
+UART3RX:414243\r\n
```

处理约束：

- 事件可能出现在等待普通 AT 命令响应期间。
- 事件是带来源信息的旁路数据，不是当前命令的数据行，也不结束当前请求。
- 上位机必须先按 `UART2RX` / `UART3RX` 路由事件，再处理普通命令的响应 FIFO。
- 事件只包含十六进制负载，不跟随 `OK`。

### 6.3 兼容路径：CRLF 文本行

为兼容 v2.1，UART1 上不被识别为 AT 命令的完整 CRLF 文本行仍会被转发到已打开的 UART2/UART3。例如：

```text
HELLO\r\n
{"cmd":"ping"}\r\n
```

该兼容路径仅推荐旧系统使用：

- 依赖 CRLF 才能形成完整行。
- 发送函数按字符串长度处理，不适合含 `0x00` 的数据。
- 只覆盖 UART1 到下游的单向文本转发；反向数据仍通过 v2.2 异步事件返回。
- 以 `AT+` 开头且被识别为控制命令的数据不会转发。

### 6.4 桥接开关与缓存

- UART2 未打开时，不向 UART2 发送，也不向上位机发布 UART2 接收事件。
- UART3 未打开时，不向 UART3 发送，也不向上位机发布 UART3 接收事件。
- 关闭某一路时会清空该路接收环形缓冲，防止旧数据在下一次打开后出现。
- 两路都关闭时发送 `AT+UARTTX` 会返回 `ERROR:UART_DISABLED`。

## 7. 推荐联调顺序

建议按下面顺序和固件联调：

1. 先验证串口连通性，只发控制命令，确认 UART1 能稳定收发。
2. 打开 UART2 或 UART3，发送一帧 `AT+UARTTX=414243`，确认下游收到 ASCII `ABC`。
3. 从下游向 UART2/UART3 发送数据，确认 UART1 收到带正确来源的 `+UART2RX` / `+UART3RX` 事件。
4. 查询一次 AT+SENSE?，确认传感任务已开始运行。
5. 测试 AT+MOTOR=WAKE、AT+MOTOR=FWD、AT+MOTOR? 的闭环。
6. 最后再联动 NMOS、`AT+CHARGE`、`AT+DRIVE`、`AT+POWER=OFF` 和故障读取。

一个最小联调流程示例：

```text
AT+UART2=ON
AT+UARTTX=414243
AT+SENSE?
AT+MOTOR=WAKE
AT+MOTOR=FWD
AT+MOTOR?
AT+MOTOR=BRAKE
AT+FAULT?
AT+NMOS1=ON
AT+CHARGE=ON
AT+POWER=OFF
AT+DRIVE=ON
```

## 8. 上位机实现建议

### 8.1 建议的发送策略

- 串口发送线程与接收线程分离
- 一次只挂起一条 AT 请求，等待回包或超时
- 对查询命令按两行响应处理，即数据行加 OK 行
- 对超过 32 字节的隧道负载分块，上一块收到 `OK` 后再发送下一块
- 无论是否启用桥接，UART1 接收端都应保持 CRLF 行解析，不要切换成裸字节接收
- 优先识别并旁路处理 `+UART2RX` / `+UART3RX`，不要让异步事件占用普通命令的待响应队列

### 8.2 建议的超时策略

可先按下面策略起步：

- 普通控制命令：200 ms 到 500 ms
- 查询命令：500 ms 到 1000 ms
- 上电后第一次 SENSE 查询：至少延后 1 s

### 8.3 建议的状态机

对于上位机，建议维护以下状态：

- 串口连接状态
- UART2 透传开关状态
- UART3 透传开关状态
- 当前隧道分块发送队列及在途帧
- 电机模式状态
- 最新传感快照
- 最新故障状态

## 9. 常见问题

### 9.1 为什么发送了命令但没回包

优先检查：

- 是否发到了 UART1，而不是 UART2/UART3
- 串口参数是否是 115200 8N1
- 是否带了行结束符
- 是否命令字使用了小写

### 9.2 为什么查询采样值不稳定

固件已对五路 1 Hz 传感 ADC 使用最近五周期滑动平均，但仍可能受以下因素影响：

- ADC 输入本身硬件噪声较大
- 传感任务每 1 秒才更新一次，完整窗口对应最近约 5 秒，不是高速采样接口
- NTC 与电池电压仍受 ADC 量化、参考电压和外围电阻误差影响
- 电机电流为了不延迟过流保护保持即时采样，因此波动不会被五周期窗口平滑

### 9.3 为什么 CURRENT_MA 看起来不像真实电流

`CURRENT_MA` 已按 DRV8874 IPROPI 镜像电流和 R19=2.5kΩ 换算。ADC 满量程对应约 2.93 A，因此更高电流会饱和；3 A 以上的强短路主要由 DRV8874 硬件保护承担。

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
