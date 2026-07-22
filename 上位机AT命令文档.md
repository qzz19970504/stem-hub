# 上位机 AT 命令文档

本文档面向上位机开发、联调和测试人员，说明当前固件支持的 AT 指令、串口收发约定、回包格式、透传规则和联调建议。

## 1. 文档范围

本文档描述的是当前仓库内已经实现的 AT 控制面，不包含未来规划中的功能。

当前已覆盖：

- UART1 AT 命令收发
- UART2 和 UART3 透传开关控制
- LED 总开关控制
- 电机模式控制与状态查询
- NMOS1、NMOS2 控制
- MP4317 控制（PA8，AT+MP4317=ON/OFF）
- LM51770 使能控制（PB3 EN/UVLO，AT+LM51770=ON/OFF）
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
- UART2 和 UART3 当前不解析 AT，只作为透传输出口。

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
ERROR:UNSUPPORTED
```

说明：

- ERROR:PARSE：格式合法但内容不匹配当前支持的命令集
- ERROR:SENSE_NOT_READY：传感任务尚未完成第一次有效采样
- ERROR:LINE_TOO_LONG：一行数据超出内部缓存限制
- ERROR:STATE_BUSY：读取状态时互斥资源暂不可用
- ERROR:LED_QUEUE：LED 控制消息入队失败
- ERROR:MOTOR_QUEUE：电机控制消息入队失败
- ERROR:OUTPUT_QUEUE：NMOS、MP4317 或 LM51770 控制消息入队失败
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
| MP4317 | AT+MP4317=ON\r\n | 拉低 PA8，MP4317 进入工作 | OK |
| MP4317 | AT+MP4317=OFF\r\n | 拉高 PA8，MP4317 关断 | OK |
| LM51770 | AT+LM51770=ON\r\n | 拉低 EN/UVLO，LM51770 进入工作 | OK |
| LM51770 | AT+LM51770=OFF\r\n | 拉高 EN/UVLO，LM51770 关断（默认状态） | OK |

### 4.2 查询类命令

| 分类 | 指令 | 作用 |
| --- | --- | --- |
| 采样 | AT+SENSE?\r\n | 查询最近一次传感采样结果 |
| 故障 | AT+FAULT?\r\n | 查询 nFAULT 和 nFLT 引脚状态 |
| 电机 | AT+MOTOR?\r\n | 查询当前电机模式、电流与故障标志 |
| 诊断 | AT+DIAG?\r\n | 查询 UART1 RX 路径关键计数器 |
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

### 5.4 NMOS、MP4317 与 LM51770 命令

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

#### 5.4.2 LM51770（PB3 EN/UVLO）

```text
AT+LM51770=ON
AT+LM51770=OFF
```

说明：

- 控制 PB3（EN/UVLO）的输出电平，进而控制 LM51770 的使能。
- LM51770 是低电平使能，所以：
  - `AT+LM51770=ON` 把 PB3 拉低，LM51770 进入工作。
  - `AT+LM51770=OFF` 把 PB3 拉高，LM51770 关断。
- 默认状态（开机 / 复位后）：PB3 高、LM51770 关断。

#### 5.4.3 MP4317（PA8 NMOS 控制）

```text
AT+MP4317=ON
AT+MP4317=OFF
```

说明：

- 控制 PA8 的输出电平。PA8 经外部 NMOS 控制 MP4317 的使能。
- MP4317 是低电平使能，所以：
  - `AT+MP4317=ON` 把 PA8 拉低，MP4317 开。
  - `AT+MP4317=OFF` 把 PA8 拉高，MP4317 关。
- 默认状态（开机 / 复位后）：PA8 高、MP4317 关。

### 5.5 传感查询命令

#### 5.5.1 请求格式

```text
AT+SENSE?
```

#### 5.5.2 响应格式

```text
+SENSE:BATT_NTC=<value>,BATT_V=<value>V,NTC1_C=<value>C,NTC2_C=<value>C,NTC3_C=<value>C,TICK=<value>,COUNT=<value>
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
| TICK | 本次样本写入时的系统 Tick |
| COUNT | 样本计数 |

NTC 拓扑为 3V3 -- NTC -- ADC测点 -- 470Ω -- GND。

BATT_NTC（电池 NTC，ADC1 IN4）使用 3435K 系 NTC，电阻分压比相同但型号与 NTC1_C/NTC2_C/NTC3_C 不同：  
- BATT_NTC 范围 -55°C ~ +125°C，钳位到表外时分别返回 -55.0C / 125.0C。读数为 0（开路）时钳位到 -55.0C，读数 ≥ 3300mV（短路）时返回 ERR。  
- NTC1/NTC2/NTC3 使用 HNTC0603-103F3450FA，范围 -40°C ~ +125°C，钳位到表外时分别返回 -40.0C / 125.0C。读数为 0（开路）时钳位到 -40.0C，读数 ≥ 3300mV（短路）时返回 ERR。

注意：

- BATT_NTC / NTC1_C / NTC2_C / NTC3_C 在 0~85°C 区间精度约 ±0.5°C，<0°C 因 ADC 量化误差较大（Vadc 已 < 25mV）。
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

- CURRENT_MA 当前是基于 ADC 电压的占位换算值，不代表最终精确毫安值。
- 当电流读数超过当前阈值时，固件会转入制动并置位 OVERCURRENT。

### 5.8 诊断查询命令

#### 5.8.1 请求格式

```text
AT+DIAG?
```

#### 5.8.2 响应格式

```text
+DIAG:RX_ISR=<n>,RX_BYTE=<n>,RX_OVERFLOW=<n>,RX_ERR=<n>,ORE=<n>,NE=<n>,FE=<n>,PE=<n>,LINE_TOO_LONG=<n>,AT_LOOP=<n>,TX_CALL=<n>,TX_OK=<n>,TX_TIMEOUT=<n>,TX_ERR=<n>,UART_WDG=<n>
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
| TX_CALL | App_RuntimeSendText 调用次数 |
| TX_OK | HAL_UART_Transmit 返回 HAL_OK 次数 |
| TX_TIMEOUT | HAL_UART_Transmit 返回 HAL_TIMEOUT 次数 |
| TX_ERR | HAL_UART_Transmit 返回 HAL_ERROR 次数 |
| UART_WDG | UART 看门狗触发复位的次数（>0 即说明出现过 UART 假死并被自愈） |

用途：

- 用于排查 “命令发出但收不到回包” 类问题。
- 计数器从 0 开始累计，不掉电不清零。
- 固件侧不阻塞、不重置这些计数器，仅在 `AT+DIAG?` 时拷快照输出。

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
+VERSION:release-v2.1
OK
```

说明：

- 用于上位机连接 UART1 后的握手。拿到 `+VERSION:...` + `OK` 即可确认固件能解析且能应答。
- `<version>` 在固件侧的 `app_config.h::APP_FIRMWARE_VERSION` 定义，bump 版本只需改这一行。
- 建议超时：500 ms 之内没拿到 `OK` 即视为握手失败。
- 版本号可用于后续命令集能力协商（例如旧版本可能不识别 `AT+LM51770=` / `AT+MP4317=`）。

## 6. 透传规则

### 6.1 什么会被透传

只有不被识别为 AT 命令的数据才会透传。例如：

```text
HELLO
123456
{"cmd":"ping"}
```

### 6.2 什么不会被透传

所有以 AT+ 开头并被当作 AT 控制面的数据，都不会透传。

例如：

```text
AT+LED=ON
AT+MOTOR=FWD
AT+SENSE?
```

### 6.3 透传前提

透传数据是否真正输出，取决于桥接开关状态：

- UART2 未打开，则不会发往 UART2
- UART3 未打开，则不会发往 UART3

### 6.4 透传限制

如果你的原始业务数据本身就是以 AT+ 开头，当前版本会优先把它当命令解析，而不是透传。

对于这种场景，建议上位机：

- 改变透传数据前缀
- 或者与固件侧另行设计专用透传通道协议

## 7. 推荐联调顺序

建议按下面顺序和固件联调：

1. 先验证串口连通性，只发控制命令，确认 UART1 能稳定收发。
2. 打开 UART2 或 UART3 透传，验证非 AT 数据转发是否正常。
3. 查询一次 AT+SENSE?，确认传感任务已开始运行。
4. 测试 AT+MOTOR=WAKE、AT+MOTOR=FWD、AT+MOTOR? 的闭环。
5. 最后再联动 NMOS、MP4317、LM51770 和故障读取。

一个最小联调流程示例：

```text
AT+UART2=ON
AT+SENSE?
AT+MOTOR=WAKE
AT+MOTOR=FWD
AT+MOTOR?
AT+MOTOR=BRAKE
AT+FAULT?
AT+NMOS1=ON
AT+MP4317=ON
AT+LM51770=ON
```

## 8. 上位机实现建议

### 8.1 建议的发送策略

- 串口发送线程与接收线程分离
- 一次只挂起一条 AT 请求，等待回包或超时
- 对查询命令按两行响应处理，即数据行加 OK 行

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

原因可能包括：

- 当前换算函数还是占位实现
- ADC 输入本身硬件噪声较大
- 传感任务每 1 秒才更新一次，不是高速采样接口

### 9.3 为什么 CURRENT_MA 看起来不像真实电流

这是当前版本的已知现状。现在只完成了 ADC 电压到占位值的映射，尚未写入真实的 IPROPI 电流换算公式。

### 9.4 为什么透传数据丢了

优先检查：

- 对应桥接是否已经打开
- 数据是否误以 AT+ 开头
- 单行是否过长

## 10. 相关文件

- [README.md](README.md)：仓库总说明
- [App/Src/app_at_protocol.c](App/Src/app_at_protocol.c)：AT 解析实现
- [App/Src/app_at_task.c](App/Src/app_at_task.c)：AT 命令执行和回包实现
- [tests/test_at_protocol.c](tests/test_at_protocol.c)：AT 解析测试