# 上位机 AT 命令文档

本文档面向上位机开发、联调和测试人员，说明当前固件支持的 AT 指令、串口收发约定、回包格式、透传规则和联调建议。

> 适用固件：`release-v3.2`
> 接口要点：固件按固定顺序发送语义化 SENSE 字段；上位机必须精确匹配 v3.2 握手，并严格要求完整、无重复的语义字段集合，不兼容旧的编号 NTC 字段。

## 1. 文档范围

本文档描述的是当前仓库内已经实现的 AT 控制面，不包含未来规划中的功能。

当前已覆盖：

- UART1 AT 命令收发
- `AT+TRANS=1/2/1&2` 选择 UART2、UART3 或双路透明传输
- 带前后至少 1 ms 静默的 `+++` 退出透明传输
- UART2/UART3 接收数据通过异步十六进制事件回传
- LED 总开关控制
- 电机模式控制与状态查询
- NMOS1、NMOS2 控制
- MCU 强制互锁的充电路径（LM51770）与驱动路径（MP4317）控制
- 60 秒周期内的充电开启时间设置与查询
- MCU、LM51770、MP4317、DRV8874 与 LM51770 充电 MOS 五路器件温度的软件过温停机
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
| 波特率 | 9600 |
| 数据位 | 8 |
| 校验位 | None |
| 停止位 | 1 |
| 流控 | None |

### 2.2 AT 命令入口

- UART1 是唯一的 AT 命令入口。
- UART2 和 UART3 不解析 AT；它们是受 UART1 控制的下游收发通道。
- UART2/UART3 的接收字节不会在中断中直接发送，而是先进入环形缓冲，再由 `bridgeTask` 封装回 UART1。
- UART1 处于透明传输时不再运行 AT 解析器，任何 AT 外观的数据都按原始负载处理。

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

注意：在 AT 模式一次只发一条命令。透明传输期间没有 AT 请求/应答；下游反向数据仍可能以 `+UART2RX:` / `+UART3RX:` 异步事件出现。

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
ERROR:OVER_TEMPERATURE
ERROR:LED_QUEUE
ERROR:MOTOR_QUEUE
ERROR:OUTPUT_QUEUE
ERROR:HEX
ERROR:UART_DISABLED
ERROR:UART_TX
ERROR:MOTOR_RUNNING
ERROR:FLASH_WRITE
ERROR:UNSUPPORTED
```

说明：

- ERROR:PARSE：格式合法但内容不匹配当前支持的命令集
- ERROR:SENSE_NOT_READY：传感任务尚未完成第一次有效采样
- ERROR:LINE_TOO_LONG：一行数据超出内部缓存限制
- ERROR:STATE_BUSY：共享保护状态或 RAM 配置的互斥资源暂不可用；危险开启命令在保护状态未知时不会放行
- ERROR:OVER_TEMPERATURE：NTC 过温保护已锁存，当前命令试图开启充电、驱动、NMOS1/2 或非 SLEEP 电机模式
- ERROR:LED_QUEUE：LED 控制消息入队失败
- ERROR:MOTOR_QUEUE：电机控制消息入队失败
- ERROR:OUTPUT_QUEUE：NMOS 或完整电源模式控制消息入队失败
- ERROR:PARSE：命令帧属于 AT 格式但不匹配当前命令集；旧的独立 LM51770/MP4317 指令也返回此错误
- ERROR:HEX：`AT+UARTTX` 的十六进制负载为空、长度非法、含非法字符或超过 32 字节
- ERROR:UART_DISABLED：AT 模式没有活动透明目标，无法执行兼容命令 `AT+UARTTX`
- ERROR:UART_TX：至少一个已选择目标的 HAL 串口发送失败
- ERROR:MOTOR_RUNNING：电机正在 FWD/REV，禁止擦写堵转阈值
- ERROR:FLASH_WRITE：堵转阈值的 Flash 擦除、编程或写后校验失败，RAM 中的生效值保持不变
- ERROR:UNSUPPORTED：解析成功但当前版本不支持该类型

## 4. 指令总览

### 4.1 控制类命令

| 分类 | 指令 | 作用 | 成功回包 |
| --- | --- | --- | --- |
| 透传 | AT+TRANS=1\r\n | 进入 UART2 透明传输 | OK |
| 透传 | AT+TRANS=2\r\n | 进入 UART3 透明传输 | OK |
| 透传 | AT+TRANS=1&2\r\n | 进入 UART2、UART3 双路透明传输 | OK |
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
| 电源路径 | AT+CHARGE=ON\r\n | 启动 60 秒周期的 LM51770 充电循环；默认 10 秒开 / 50 秒关 | OK |
| 电源路径 | AT+CHARGE=OFF\r\n | 同时关闭 LM51770 与 MP4317 | OK |
| 电源路径 | AT+DRIVE=ON\r\n | 先关闭两路，再仅打开 MP4317 | OK |
| 电源路径 | AT+DRIVE=OFF\r\n | 同时关闭 LM51770 与 MP4317 | OK |
| 电源路径 | AT+POWER=OFF\r\n | 同时关闭 LM51770 与 MP4317 | OK |
| 充电配置 | AT+CHARGE_TIME=&lt;n&gt;\r\n | 设置每个周期的 ON 秒数，`n` 为 1～60 | OK |
| 电机保护 | AT+STALL_CURRENT=&lt;mA&gt;\r\n | 设置并持久化堵转阈值，`mA` 为 1000～30000；仅限非 FWD/REV | OK |

### 4.2 查询类命令

| 分类 | 指令 | 作用 |
| --- | --- | --- |
| 采样 | AT+SENSE?\r\n | 查询最近传感快照；七路 1 Hz 传感 ADC 为最近五个完整周期的同步均值 |
| 故障 | AT+FAULT?\r\n | 查询 nFAULT 和 nFLT 引脚状态 |
| 电机 | AT+MOTOR?\r\n | 查询当前电机模式、电流与故障标志 |
| 诊断 | AT+DIAG?\r\n | 查询控制链路、TX、传感任务及 UART2/UART3 RX 计数器 |
| 充电配置 | AT+CHARGE_TIME=?\r\n | 查询 RAM 内当前配置；返回 `+CHARGE_TIME:<n>` 后跟 `OK` |
| 电机保护 | AT+STALL_CURRENT=?\r\n | 查询当前堵转阈值；返回 `+STALL_CURRENT:<mA>` 后跟 `OK` |
| 版本 | AT+VERSION?\r\n | 查询固件版本号，用于握手 |

### 4.3 已删除的旧写法

`AT+UART2/UART3/UART2&3=ON/OFF` 以及 `AT+UART23=ON/OFF` 均不再接受，发送后返回 `ERROR:PARSE`。上位机必须改用 `AT+TRANS=1/2/1&2`，并使用受保护的 `+++` 退出。

## 5. 各指令详细说明

### 5.1 透传控制命令

#### 5.1.1 进入透明传输

请求：

```text
AT+TRANS=1
AT+TRANS=2
AT+TRANS=1&2
```

三个值依次选择 UART2、UART3、UART2 与 UART3。命令成功后先响应：

```text
OK
```

从该 `OK` 之后开始，UART1 字节直接转发到目标，不依赖 CRLF，也支持 `0x00`、CR、LF、`0xFF` 等任意二进制值。双路模式把同一份字节依次发送到 UART2 和 UART3。

#### 5.1.2 AT 与透明传输互斥

透明传输期间完整 AT 解析器停止工作。例如发送 `AT+MOTOR=FWD\r\n` 时，下游会收到这些原始字节，但 MCU 不会启动电机，也不会返回 AT 回包。需要执行电机、电源、参数或查询命令时，必须先退出透明传输。

过温、堵转和充电周期等独立任务继续运行；透明传输只屏蔽人工 AT 操作，不关闭自主安全保护。

#### 5.1.3 使用受保护的 `+++` 退出

有效退出必须同时满足：

1. `+++` 前至少 1 ms 没有 UART1 数据；
2. 三个 `+` 连续出现；
3. `+++` 后至少 1 ms 没有 UART1 数据。

有效序列不会转发到下游，固件退出透明传输、清除目标并通过 UART1 返回 `OK\r\n`。随后才可再次发送 AT 命令。

如果任一静默条件不满足，候选字节会完整补发。例如 `abc+++def`、`++++` 或紧跟其他数据的 `+++X` 都不会误退出，也不会丢失其中的 `+`。

#### 5.1.4 `AT+UARTTX` 兼容说明

解析器仍识别 `AT+UARTTX=<HEX>`，但 AT 模式下没有活动透传目标，因此返回 `ERROR:UART_DISABLED`；进入透明传输后该字符串只是原始负载。新上位机不应再用它发送数据。

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
- 固件只允许三种物理状态：两路全关、仅 LM51770 开、仅 MP4317 开。
- `AT+CHARGE=ON` 启动固定 60 秒周期：先把 PB3、PA8 都置为关断电平，再仅拉低 PB3 `n` 秒，随后两路全关 `60-n` 秒，然后重复；`n` 默认是 10。
- 在任一开启段或关闭段重复发送 `AT+CHARGE=ON` 都不会重置当前阶段的截止时间。
- CHARGE 成功回包表示循环请求已入队；上位机 CHARGE 开关表示循环已启用，不表示 PB3 此刻必为开启电平。
- `AT+DRIVE=ON` 先把 PB3、PA8 都置为关断电平，再仅拉低 PA8。
- `AT+CHARGE=OFF`、`AT+DRIVE=OFF`、`AT+POWER=OFF` 都立即取消充电循环并关闭两路；`AT+DRIVE=ON` 也会立即取消循环并进入驱动状态。
- MCU 复位后保持两路全关，不自动恢复充电循环。
- 旧的 `AT+LM51770=ON/OFF` 与 `AT+MP4317=ON/OFF` 已删除，不能绕过 MCU 互锁。

#### 5.4.3 充电时间设置与查询

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
- `n=60` 表示连续 ON。内部仍以 60 秒为周期边界接收后续配置，但边界处不会切换 EN，因此不会每分钟重新软启动 LM51770。
- CHARGE 开关表示间歇循环已启用，不表示 EN 在查询时刻必然开启。

#### 5.4.4 器件 NTC 软件过温停机

- 判断使用 MCU_C、LM51770_C、MP4317_C、DRV8874_C 和 CHARGE_MOS_C；BATT_NTC 仅显示。七路采样全部成功时，输入与 `AT+SENSE?` 相同，来自最近五个完整周期的同步平均值。若 BATT_NTC 或 BATT_V 读取失败而五路器件通道成功，保护使用正式同步窗口加本周期器件样本得到的只读预览均值，不推进窗口，也不发布 SENSE。
- 任一路严格高于 60.0°C、转换为 `ERR`，或受保护 NTC 的 ADC 读取失败，都会锁存保护并停止 CHARGE/DRIVE、关闭 NMOS1/2、让电机进入 SLEEP；LED 不变。
- 高优先级停机消息之外，输出和电机消费者每 100 ms 自检一次，保护期间不会执行旧队列里的开启命令。
- 五路受保护器件温度必须全部有效且都不高于 55.0°C 才解除锁存。解除后需要上位机重新发送开启命令，固件不会自动恢复停机前状态。
- 保护期间允许关闭类命令、`AT+MOTOR=SLEEP`、所有查询和 `CHARGE_TIME` 设置/查询；危险开启命令返回 `ERROR:OVER_TEMPERATURE`。没有新增温度状态查询或阈值调参 AT 指令。

安全边界：可调占空和 NTC 停机是软件保护，不包含充电电流、累计充电时长或硬件故障锁存，也不能替代硬件限流、功率器件选型和散热设计。查明实际电流和损坏原因前，禁止无人值守满功率带载。

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

| 字段 | 含义 |
| --- | --- |
| BATT_NTC | 电池 NTC 温度（℃），PA4 / 物理引脚 14 / ADC1 IN4；保留 1 位小数或 ERR。使用独立 3435K 表，仅显示。 |
| BATT_V | 电池电压（ADC1 IN5），单位为伏特（V），保留 1 位小数（如 3.3V、37.0V）。已考虑 100kΩ + 5kΩ 电阻分压（实际倍率 21）。 |
| MCU_C | MCU 温度（℃），PB1 / 物理引脚 19 / ADC2 IN9；保留 1 位小数或 ERR。 |
| LM51770_C | LM51770 温度（℃），PA7 / 物理引脚 17 / ADC2 IN7；格式同 MCU_C。 |
| MP4317_C | MP4317 温度（℃），PA6 / 物理引脚 16 / ADC2 IN6；格式同 MCU_C。 |
| DRV8874_C | DRV8874 温度（℃），PA1 / 物理引脚 11 / ADC2 IN1；格式同 MCU_C。 |
| CHARGE_MOS_C | LM51770 充电 MOS 温度（℃），PA0 / 物理引脚 10 / ADC1 IN0；格式同 MCU_C。 |
| MOTOR_I | DRV8874 IPROPI 电流，保留 1 位小数（如 0.8A、19.0A）。0.1 A 分辨率，最高约 33.3 A（ADC 物理满量程；电机不在 FWD/REV 时固定为 0.0A）。 |
| TICK | 本次样本写入时的系统 Tick |
| STK_AT / STK_SENSOR / STK_MOTOR | atTask / sensorTask / motorTask 栈高水位（word），被动观测字段 |
| TX_SP / TX_LS | 最近一次发送前的 `huart->gState` 与返回值（HAL 状态机快照），被动观测字段 |
| COUNT | 样本计数 |

五路器件 NTC 均使用 HNTC0603-103F3450FA（R25=10kΩ，B25/85=3450K）及共同拓扑 `3V3 -- NTC -- ADC测点 -- 470Ω -- GND`，共用 -40°C ~ +125°C 查找表。

BATT_NTC 使用独立的 3435K 系 NTC 表，与五路 HNTC0603-103F3450FA 器件表分开：
- BATT_NTC 范围 -55°C ~ +125°C，钳位到表外时分别返回 -55.0C / 125.0C。读数为 0（开路）时钳位到 -55.0C，读数 ≥ 3300mV（短路）时返回 ERR。  
- 五路器件 NTC 范围 -40°C ~ +125°C，钳位到表外时分别返回 -40.0C / 125.0C。读数为 0（开路）时钳位到 -40.0C，读数 ≥ 3300mV（短路）时返回 ERR。

MOTOR_I 来自 DRV8874 IPROPI 镜像电流链路：  
- 拓扑：IPROPI -- [R19=220Ω] -- GND → ADC2 IN8。
- 公式：`I(A) = V_IPROPI(V) / (AIPROPI × R19) = V_IPROPI(V) / 0.099`，即 `I_mA = V_mV × 1000 / 99`（AIPROPI 取 450 µA/A）。
- 寄存器输出 0.1 A 分辨率整数 (deci-A)：`motor_current_a_deci = round(mA / 100)`，上限钳到 333 (= 33.3 A)。
- 仅当电机在 FWD/REV 时刷值；SLEEP/WAKE/BRAKE/STOP 一律为 0。  
- 电机任务以 10 ms 周期读取该瞬时值用于堵转判定；`AT+SENSE?` 仍按传感快照的 1 Hz 节奏发布，不代表保护只有 1 Hz。

注意：

- BATT_NTC 与五路器件 NTC 在 0~85°C 区间精度约 ±0.5°C，<0°C 因 ADC 量化误差较大（Vadc 已 < 25mV）。
- BATT_NTC、BATT_V、MCU_C、LM51770_C、MP4317_C、DRV8874_C、CHARGE_MOS_C 组成同步七通道 1 Hz 窗口；只有七路在同一周期全部成功时才共同推进并发布，任何部分周期都不推进、不发布。每路先对最近五个完整周期的原始值求均值再换算，启动前四个完整周期按已有完整样本数计算。
- 七个固定窗口和运行和位于静态 RAM，不在 sensorTask 栈上创建快照；单路运行和最大为 20475。
- 字段顺序固定为 `BATT_NTC,BATT_V,MCU_C,LM51770_C,MP4317_C,DRV8874_C,CHARGE_MOS_C,MOTOR_I,TICK,COUNT,STK_AT,STK_SENSOR,STK_MOTOR,TX_SP,TX_LS`；v3.2 不提供旧编号 NTC 字段的兼容别名。
- MOTOR_I 由电机状态产生，sensorTask 每秒最多更新一次；上次残值不影响停机显示（电机模式 ≠ FWD/REV 时强制归零）。
- MOTOR_I 参与堵转保护，不使用五周期滑动平均。
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

- CURRENT_MA 基于 220 Ω IPROPI 链路换算 (`I_mA = V_mV × 1000 / 99`)，单位为毫安整数。Vref=3.3V 下最大量程约 33.3 A。需 0.1 A 分辨率或停机归零语义请用 `AT+SENSE?` 的 `MOTOR_I` 字段。
- 电机启动后先屏蔽 300 ms；随后每 10 ms 采样，连续不低于当前阈值 100 ms 才转入 BRAKE 并置位 OVERCURRENT。触发时 CURRENT_MA 保留该次停机电流。
- 新的 FWD 或 REV 均会清除旧 OVERCURRENT 锁存并允许启动；若机械堵塞仍存在，屏蔽窗口结束后会再次触发。SLEEP/WAKE/BRAKE/STOP 不清除锁存。

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

- 默认值为 4000 mA。设置成功后写入 STM32 最后一页 1 KiB Flash；记录含 magic、格式版本和 CRC32，未初始化或校验失败时上电回退到 4000 mA。
- FWD/REV 时设置返回 `ERROR:MOTOR_RUNNING`，避免运行期间擦写 Flash；查询始终允许。设置相同值直接返回 `OK`，不重复擦写。
- 擦除、半字编程或写后校验失败返回 `ERROR:FLASH_WRITE`；共享状态暂不可用返回 `ERROR:STATE_BUSY`。超范围、负数、小数、带单位或其他格式返回 `ERROR:PARSE`。
- 阈值配置不改变当前运行阶段；由于只允许非运行时写入，下一次 FWD/REV 使用新值。
- 软件停机不能替代 DRV8874 自身硬件保护、保险及机械限位。验证应使用较低阈值或可控负载，不故意制造理论 19 A 的硬堵转。

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
+VERSION:release-v3.2
OK
```

说明：

- 用于上位机连接 UART1 后的握手；当前上位机只接受精确的 `+VERSION:release-v3.2` 后跟 `OK`。
- `<version>` 在固件侧的 `app_config.h::APP_FIRMWARE_VERSION` 定义，bump 版本只需改这一行。
- 建议超时：500 ms 之内没拿到 `OK` 即视为握手失败。
- v3.2 使用 `AT+CHARGE`、`AT+DRIVE` 和 `AT+POWER=OFF`；`CHARGE=ON` 的 UI 状态表示间歇循环已启用。
- v3.2 上位机严格解析语义化 SENSE 字段，不兼容旧的编号 NTC 接口。
- v2.2 相比 v2.1 新增的 `AT+UARTTX`、`+UART2RX`、`+UART3RX` 和四个 UART2/UART3 接收诊断字段在 v3.0 中继续保留。
- 版本号可用于命令集能力判断；v2.1 上位机不能假定固件支持双向二进制隧道，v2.2 上位机也不能假定独立电源芯片指令仍有效。

## 6. 双向透明传输规则

### 6.1 UART1 到下游：原始字节

进入透明传输后，上位机直接写原始字节。该路径具有以下性质：

- 二进制安全，包括 `0x00`、CR、LF 和 `0xFF`。
- 不依赖负载本身携带行结束符。
- 普通数据块不产生 `OK` 或 `ERROR` 回包。
- `AT+TRANS=1&2` 可把同一数据发送到 UART2 和 UART3。
- AT 外观的数据不会触发 MCU 控制动作。

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

### 6.3 模式切换与缓存

- 每次 `AT+TRANS` 都精确选择一个目标组合，不累加以前的选择。
- 进入和退出时会清空 UART2/UART3 尚未上报的接收缓存，防止旧数据混入新会话。
- 有效 `+++` 退出后两路目标都关闭，UART1 恢复 CRLF AT 行解析。
- UART1 接收溢出会中止透明传输、清除目标并返回 `ERROR:RX_OVERFLOW`，避免在已丢字节的数据流上继续误判协议。

## 7. 推荐联调顺序

建议按下面顺序和固件联调：

1. 先验证串口连通性，只发控制命令，确认 UART1 能稳定收发。
2. 用 `AT+TRANS=1` 或 `AT+TRANS=2` 进入透明传输，直接发送 ASCII `ABC`，确认对应下游收到原始字节。
3. 从下游向 UART2/UART3 发送数据，确认 UART1 收到带正确来源的 `+UART2RX` / `+UART3RX` 事件。
4. 前后各静默至少 1 ms 发送 `+++`，收到 `OK` 后查询一次 `AT+SENSE?`。
5. 测试 AT+MOTOR=WAKE、AT+MOTOR=FWD、AT+MOTOR? 的闭环。
6. 最后再联动 NMOS、`AT+CHARGE`、`AT+DRIVE`、`AT+POWER=OFF` 和故障读取。

一个最小联调流程示例：

```text
AT+TRANS=1
<原始 ASCII ABC>
<静默至少 1 ms>+++<静默至少 1 ms>
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
- 进入透明传输后切换为原始字节发送；不要等待普通数据块的 AT 回包
- 退出时在 `+++` 前后各保持至少 1 ms 无发送数据，并等待退出 `OK`
- 优先识别并旁路处理 `+UART2RX` / `+UART3RX`，不要让异步事件占用普通命令的待响应队列

### 8.2 建议的超时策略

可先按下面策略起步：

- 普通控制命令：200 ms 到 500 ms
- 查询命令：500 ms 到 1000 ms
- 上电后第一次 SENSE 查询：至少延后 1 s

### 8.3 建议的状态机

对于上位机，建议维护以下状态：

- 串口连接状态
- 当前模式（AT / 透明传输）
- 当前透明目标（UART2 / UART3 / 双路）
- 退出静默计时与在途原始数据
- 电机模式状态
- 最新传感快照
- 最新故障状态

## 9. 常见问题

### 9.1 为什么发送了命令但没回包

优先检查：

- 是否发到了 UART1，而不是 UART2/UART3
- 串口参数是否是 9600 8N1
- 当前是否仍处于透明传输；此时 AT 指令只会被转发，不会回包
- 是否带了行结束符
- 是否命令字使用了小写

### 9.2 为什么查询采样值不稳定

固件已对七路同步 1 Hz 传感 ADC 使用最近五个完整周期的滑动平均，但仍可能受以下因素影响：

- ADC 输入本身硬件噪声较大
- 传感任务每 1 秒才更新一次，完整窗口对应最近约 5 秒，不是高速采样接口
- NTC 与电池电压仍受 ADC 量化、参考电压和外围电阻误差影响
- 电机电流为了不延迟过流保护保持即时采样，因此波动不会被五周期窗口平滑

### 9.3 为什么 CURRENT_MA 看起来不像真实电流

`CURRENT_MA` 已按 DRV8874 IPROPI 镜像电流和 R19=220Ω 换算，3.3 V ADC 满量程约 33.3 A。确认 R19 实装值、ADC 参考电压和 IPROPI 连线；堵转保护默认阈值为 4000 mA，可用 `AT+STALL_CURRENT=?` 查询。

### 9.4 为什么透传数据丢了

优先检查：

- `AT+TRANS` 是否选择了正确目标（`1`=UART2，`2`=UART3）
- 是否误把原始数据继续编码成 `AT+UARTTX`
- 是否把 `+UART2RX` / `+UART3RX` 异步事件误认为正向负载
- `UART2_RX_OVERFLOW` / `UART3_RX_OVERFLOW` 是否增长
- 是否意外发送了满足前后静默条件的独立 `+++`

## 10. 相关文件

- [README.md](README.md)：仓库总说明
- [App/Src/app_at_protocol.c](App/Src/app_at_protocol.c)：AT 解析实现
- [App/Src/app_at_task.c](App/Src/app_at_task.c)：AT 命令执行和回包实现
- [App/Src/app_bridge_task.c](App/Src/app_bridge_task.c)：UART2/UART3 接收缓冲排空和事件上报
- [App/Src/app_uart_tunnel.c](App/Src/app_uart_tunnel.c)：异步接收事件编码
- [tests/test_at_protocol.c](tests/test_at_protocol.c)：AT 解析测试
- [tests/test_uart_tunnel.c](tests/test_uart_tunnel.c)：隧道事件编码测试
