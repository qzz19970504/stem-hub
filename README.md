# stem-hub

基于 STM32F103C8T6 和 FreeRTOS 的嵌入式控制项目，当前聚焦于多路 ADC 采集、DRV8874 电机驱动、UART AT 指令控制，以及 UART1 与 UART2/UART3 之间的可控双向二进制隧道。

当前固件版本为 `release-v3.2`。本版使用语义化温度通道，增加 MCU、LM51770、MP4317、DRV8874 和 LM51770 充电 MOS 五路器件温度。固件按固定顺序发送 SENSE 字段；v3.2 上位机严格要求完整、无重复的语义字段集合。

## 快速开始

如果你只是想先把工程编译起来，按下面顺序做即可。

1. 安装依赖工具。

   - CMake 3.22 或更高版本
   - Ninja
   - arm-none-eabi-gcc 工具链
   - 可选：STM32CubeMX，用于重新生成外设初始化代码

2. 确认工具在 PATH 中可用。

   ```powershell
   cmake --version
   ninja --version
   arm-none-eabi-gcc --version
   ```

   你应该能看到三个命令都返回版本号。

3. 配置 Debug 构建目录。

   ```powershell
   cmake --preset Debug
   ```

   你应该能看到 CMake 配置完成，并在 build/Debug 下生成构建文件。

4. 编译固件。

   ```powershell
   cmake --build --preset Debug
   ```

   你应该能看到 `.elf` 目标完成链接，没有编译错误，并在 `build/Debug` 下同时得到 `stem-hub.elf`、`stem-hub.hex` 和 `stem-hub.bin`。

5. 可选：运行 AT 协议和隧道编码的主机侧测试。

   这两项测试验证 AT 解析和 UART 事件编码，不依赖 MCU 硬件。

   ```powershell
   gcc -std=c11 -Wall -Wextra -IApp/Inc tests/test_at_protocol.c App/Src/app_at_protocol.c -o tests/test_at_protocol.exe
   .\tests\test_at_protocol.exe
   gcc -std=c11 -Wall -Wextra -IApp/Inc tests/test_uart_tunnel.c App/Src/app_uart_tunnel.c -o tests/test_uart_tunnel.exe
   .\tests\test_uart_tunnel.exe
   ```

   正常情况下命令无输出并返回 0。

## 这个项目做什么

当前工程已经围绕以下目标搭起了应用层框架：

- UART1 作为主控制串口，使用非阻塞中断接收 AT 指令。
- UART2、UART3 可按 AT 命令独立开关；上位机可用 `AT+UARTTX=<HEX>` 精确发送二进制数据，UART2/UART3 收到的数据会以 `+UART2RX:<HEX>` / `+UART3RX:<HEX>` 异步回传。
- 保留 v2.1 的 CRLF 行文本转发路径，用于兼容旧上位机；新上位机应优先使用 v2.2 的十六进制帧协议。
- 使用 ADC1 和 ADC2 采样电池 NTC、电池电压、5 路器件 NTC 电压，以及电机电流检测通道。
- 使用 FreeRTOS 任务划分 AT 解析、传感采样、电机控制、LED 控制、NMOS 控制。
- 通过 DRV8874 的 PH/EN 模式控制电机正转、反转、制动、睡眠，并加入换向死区和可持久化的堵转电流保护。

## 当前能力边界

下面区分已经落地的换算与仍未覆盖的系统能力，不把未来规划写成已完成：

- BATT_NTC（电池 NTC，PA4 / 物理引脚 14 / ADC1 IN4）使用独立的 3435K 查找表（R25=10kΩ，-55~+125°C，1°C 步长，线性插值），仅显示，不参与过温保护。
- MCU、LM51770、MP4317、DRV8874 和 CHARGE_MOS 五路器件 NTC 均为 HNTC0603-103F3450FA（R25=10kΩ，B25/85=3450K），共用 `3V3 -- NTC -- ADC -- 470 Ω -- GND` 拓扑和同一张 -40~+125°C 查找表。
- nFAULT 和 nFLT 目前只支持 GPIO 读取和查询，没有完整故障恢复流程。
- 没有提供下载、烧录、量产参数配置脚本。
- 没有引入 DMA 串口接收或 ADC DMA 扫描，当前实现以简单、稳定、容易维护为优先。
- 默认充电周期仍为 10 秒开 / 50 秒关；五路器件温度均受软件过温停机保护。这仍不替代硬件限流、器件选型和独立温度保护；禁止无人值守带载，带载前仍必须核查实际充电电流、MOSFET、电感饱和、限流设定和散热设计。

## 先决条件

### 硬件

- MCU：STM32F103C8T6
- 电机驱动：DRV8874，PH/EN 模式
- 调试下载：ST-LINK 或等效 SWD 工具
- 上位机串口工具：任意支持 9600 8N1 的串口终端

### 软件

- Windows PowerShell 或其他可用终端
- CMake 3.22+
- Ninja
- GNU Arm Embedded Toolchain
- 可选：MinGW GCC，用于运行 tests 目录下的主机侧 C 测试

### 校验命令

```powershell
cmake --version
ninja --version
arm-none-eabi-gcc --version
gcc --version
```

如果其中某个命令不存在，先把对应工具加入 PATH，再继续。

## 环境搭建

### 1. 获取代码

```powershell
git clone <你的仓库地址>
cd stem-hub
```

### 2. 检查工具链文件

本工程默认使用 [cmake/gcc-arm-none-eabi.cmake](cmake/gcc-arm-none-eabi.cmake) 作为交叉编译工具链文件，并通过 [CMakePresets.json](CMakePresets.json) 提供 Debug 和 Release 两套预设。

### 3. 生成并编译

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

如果你要编译发布版本：

```powershell
cmake --preset Release
cmake --build --preset Release
```

### 4. 重新生成 CubeMX 代码时的注意事项

- 用户业务逻辑尽量放在 USER CODE 区域或独立的应用文件中。
- 当前应用层主要在顶层 App/Inc 和 App/Src 目录下，与 Core 生成层分离。
- 如果新增了自己的 .c 文件，记得同步更新 [cmake/stm32cubemx/CMakeLists.txt](cmake/stm32cubemx/CMakeLists.txt)，否则 CMake 不会编译这些源文件。

## 使用说明

### 串口配置

三个 UART 当前都配置为：

- 波特率：9600
- 数据位：8
- 校验位：None
- 停止位：1

UART1 为主控制入口，UART2 和 UART3 用作可开关的下游通信口。控制命令、隧道发送和异步接收事件都复用 UART1，但保持明确的帧边界。

### AT 语法约束

当前固件对 AT 指令采用严格语法检查，必须同时满足以下条件：

- 只能使用大写字母命令字和值
- 指令中不能包含空格或制表符
- 每条 AT 指令必须以 `\r\n` 结尾

例如，下面是合法输入：

```text
AT+LED=ON\r\n
AT+SENSE?\r\n
```

下面这些当前都会被拒绝：

```text
AT+LED=ON
AT +LED=ON\r\n
AT+LED =ON\r\n
at+led=on\r\n
```

### 已实现的 AT 指令

#### 控制类

| 指令 | 说明 |
| --- | --- |
| AT+UART2=ON | 打开 UART2 透传 |
| AT+UART2=OFF | 关闭 UART2 透传 |
| AT+UART3=ON | 打开 UART3 透传 |
| AT+UART3=OFF | 关闭 UART3 透传 |
| AT+UART2&3=ON | 同时打开 UART2 和 UART3 透传 |
| AT+UART2&3=OFF | 同时关闭 UART2 和 UART3 透传 |
| AT+UARTTX=&lt;HEX&gt; | 向当前已打开的 UART2/UART3 发送 1～32 字节原始数据 |
| AT+LED=ON | 打开 LED 联动显示 |
| AT+LED=OFF | 关闭 LED 联动显示 |
| AT+MOTOR=SLEEP | 电机进入睡眠 |
| AT+MOTOR=WAKE | 唤醒电机驱动，不输出转动 |
| AT+MOTOR=FWD | 电机正转 |
| AT+MOTOR=REV | 电机反转 |
| AT+MOTOR=BRAKE | 电机制动 |
| AT+MOTOR=STOP | 电机停止，当前实现等同制动 |
| AT+MOTOR_BYPASS=ON | 仅在 FWD/REV 已启动后拉高 PC13，短接电机启动限流电阻 |
| AT+MOTOR_BYPASS=OFF | 拉低 PC13，恢复电机启动限流电阻 |
| AT+NMOS1=ON | 打开 NMOS1 |
| AT+NMOS1=OFF | 关闭 NMOS1 |
| AT+NMOS2=ON | 打开 NMOS2 |
| AT+NMOS2=OFF | 关闭 NMOS2 |
| AT+CHARGE=ON | 启动 LM51770 的 60 秒间歇充电循环；默认 10 秒开 / 50 秒关 |
| AT+CHARGE=OFF | 同时关闭 LM51770 与 MP4317 |
| AT+CHARGE_BYPASS=ON | 仅在 LM51770 实际 ON 相位拉高 PC14，短接充电限流电阻 |
| AT+CHARGE_BYPASS=OFF | 拉低 PC14，恢复充电限流电阻 |
| AT+DRIVE=ON | 先关闭 LM51770 与 MP4317，再仅打开 MP4317 |
| AT+DRIVE=OFF | 同时关闭 LM51770 与 MP4317 |
| AT+POWER=OFF | 同时关闭 LM51770 与 MP4317 |
| AT+CHARGE_TIME=&lt;n&gt; | 设置每个 60 秒周期的充电开启时间，`n` 为 1～60 秒 |
| AT+STALL_CURRENT=&lt;mA&gt; | 设置堵转电流阈值，范围 1000～30000 mA；仅允许电机非 FWD/REV 时设置并写入 Flash |

#### 查询类

| 指令 | 说明 | 回包示例 |
| --- | --- | --- |
| AT+SENSE? | 读取最近传感快照；七路 1 Hz 传感 ADC 为最近五个完整周期的同步均值 | +SENSE:BATT_NTC=25.1C,BATT_V=37.0V,MCU_C=25.3C,LM51770_C=25.2C,MP4317_C=25.4C,DRV8874_C=26.1C,CHARGE_MOS_C=24.8C,MOTOR_I=0.8A,TICK=4567,COUNT=8,STK_AT=512,STK_SENSOR=384,STK_MOTOR=420,TX_SP=0,TX_LS=0 |
| AT+FAULT? | 读取 nFAULT 和 nFLT 状态 | +FAULT:DRV=0,AUX=0 |
| AT+MOTOR? | 读取电机当前模式、电流和故障状态 | +MOTOR:MODE=FWD,CURRENT_MA=820,OVERCURRENT=0,FAULT=0 |
| AT+DIAG? | 读取 UART1 控制链路、发送状态、传感任务及 UART2/UART3 接收计数器 | +DIAG:RX_ISR=1234,...,UART2_RX_BYTE=20,UART2_RX_OVERFLOW=0,UART3_RX_BYTE=12,UART3_RX_OVERFLOW=0 |
| AT+CHARGE_TIME=? | 查询当前 RAM 内充电开启时间 | +CHARGE_TIME:10 |
| AT+STALL_CURRENT=? | 查询当前堵转电流阈值（mA） | +STALL_CURRENT:4000 |
| AT+VERSION? | 读取固件版本号（用于上位机握手） | +VERSION:release-v3.2 |

控制类命令成功时返回：

```text
OK
```

解析失败或运行失败时返回：

```text
ERROR
ERROR:PARSE
ERROR:SENSE_NOT_READY
ERROR:LINE_TOO_LONG
ERROR:STATE_BUSY
ERROR:STATE
ERROR:OVER_TEMPERATURE
ERROR:MOTOR_RUNNING
ERROR:FLASH_WRITE
```

`ERROR:OVER_TEMPERATURE` 表示 NTC 保护锁存期间拒绝了危险的开启动作。`ERROR:STATE` 表示旁路 ON 的运行条件不成立，例如电机未处于 FWD/REV，或充电当前不在实际 ON 相位。`ERROR:STATE_BUSY` 表示固件暂时无法读取共享保护状态或访问 RAM 配置；对于需要开启输出的命令，固件按安全失败处理，不会在状态未知时放行。`ERROR:MOTOR_RUNNING` 表示 FWD/REV 运行期间拒绝写堵转阈值，`ERROR:FLASH_WRITE` 表示非易失写入或写后校验失败。

### 限流电阻旁路语义

- PC13（STM32F103C8T6 物理 2 脚）为推挽输出，默认低。电机先以限流电阻启动；只有状态已是 FWD/REV 时 `AT+MOTOR_BYPASS=ON` 才能拉高 PC13。STOP、BRAKE、SLEEP、堵转、过温或 FWD/REV 换向都会先恢复低电平；换向完成后必须再次发送 ON。
- PC14（物理 3 脚）为推挽输出，默认低。只有 LM51770 处于充电周期的实际 ON 相位时 `AT+CHARGE_BYPASS=ON` 才能拉高 PC14。进入周期 OFF 相位、退出 CHARGE、切到 DRIVE、过温或任何电源路径阶段切换都会先恢复低电平；下一个 ON 相位不会自动旁路。
- 两条 OFF 指令始终是安全关闭请求。普通 AT 指令和 UART2/UART3 通信在电机运行时仍可使用；旁路功能不是独占通信模式。

### 充电时间语义

- `AT+CHARGE_TIME=n` 只接受十进制整数 1～60，成功返回 `OK`；`AT+CHARGE_TIME=?` 精确返回 `+CHARGE_TIME:<n>\r\nOK\r\n`。
- 周期固定为 60 秒，ON 为 `n` 秒，OFF 为 `60-n` 秒；默认 `n=10`。配置只存 RAM，MCU 复位后恢复 10 秒开 / 50 秒关。
- 运行中设置新值不会修改当前 ON/OFF 相位或绝对截止时间；从下一次 ON 相位开始，整个新周期使用新值。
- `n=60` 表示连续开启。调度器仍在内部经过 60 秒周期边界以接收待生效配置，但边界处不切换 LM51770 EN，避免重复软启动。
- `AT+CHARGE=ON` 的成功回包表示循环请求已接受；上位机 CHARGE 开关表示循环已启用，不表示 LM51770 EN 此刻必然处于 ON 相位。

### v2.2 双向 UART 隧道

推荐的新协议如下：

- 先用 `AT+UART2=ON`、`AT+UART3=ON` 或 `AT+UART2&3=ON` 选择目标。
- 上位机发送 `AT+UARTTX=<HEX>\r\n`；`<HEX>` 必须是 2～64 个大写十六进制字符，对应 1～32 字节原始数据。
- 如果两个目标都打开，同一份原始数据会发送到 UART2 和 UART3；全部目标发送成功后才返回 `OK`。
- UART2/UART3 收到的数据由中断写入独立环形缓冲，再由 `bridgeTask` 在任务上下文按最多 32 字节封装为 `+UART2RX:<HEX>\r\n` 或 `+UART3RX:<HEX>\r\n`。
- `+UART2RX` / `+UART3RX` 是异步事件，不带额外 `OK`，上位机不得把它们当作当前 AT 请求的响应终止行。
- 关闭某一路桥接时，固件会清空该路尚未回传的接收缓冲，避免重新打开后混入旧数据。

示例：

```text
AT+UART2=ON\r\n
OK\r\n
AT+UARTTX=00FF0D0A\r\n
OK\r\n
+UART2RX:414243\r\n
```

兼容行为：v2.1 的 UART1 非 AT、CRLF 行文本仍可单向转发到已启用的 UART2/UART3，但它依赖行结束符和字符串长度，不适合包含 `0x00` 的二进制数据，也不提供原始反向通道。

### 上位机握手

上位机连上 UART1 后，先发一次 `AT+VERSION?`，约定如下：

- 成功回包：单行 `+VERSION:<version>` 后跟 `OK`。
- 例：`+VERSION:release-v3.2\r\nOK\r\n`。
- 当前上位机要求精确匹配 `release-v3.2`；匹配后使用语义化 SENSE 字段，不兼容旧的编号 NTC 字段。
- 超时建议：500 ms 之内没拿到 `OK` 即视为握手失败。

### 传感采样说明

当前传感任务每 1 秒采样一次，采样内容如下：

| 语义量 | MCU 引脚 / 物理引脚 | ADC 通道 | NTC / 用途 |
| --- | --- | --- | --- |
| BATT_NTC | PA4 / 14 | ADC1 IN4 | 电池 NTC，独立 3435K 表，仅显示 |
| BATT_V | PA5 | ADC1 IN5 | 电池电压 |
| MCU_C | PB1 / 19 | ADC2 IN9 | HNTC0603-103F3450FA |
| LM51770_C | PA7 / 17 | ADC2 IN7 | HNTC0603-103F3450FA |
| MP4317_C | PA6 / 16 | ADC2 IN6 | HNTC0603-103F3450FA |
| DRV8874_C | PA1 / 11 | ADC2 IN1 | HNTC0603-103F3450FA |
| CHARGE_MOS_C | PA0 / 10 | ADC1 IN0 | LM51770 充电 MOS，HNTC0603-103F3450FA |
| MOTOR_I | PB0 | ADC2 IN8 | 电机电流检测，供电机任务使用 |

说明：

- ADC2_IN8 不参与 1Hz 传感结构刷新，而是留给电机电流检测使用。
- ADC2 访问通过互斥锁保护，避免传感任务和电机任务竞争同一外设。
- BATT_NTC、BATT_V 与五路器件 NTC 组成同步七通道 1 Hz 滚动窗口。只有七路在同一周期全部成功时才同时推进并发布；任何部分周期都不得推进或发布。每路使用最近五个完整周期的原始值求均值后再换算物理量，启动前四个完整周期按已有完整样本数求均值。
- 七个窗口使用静态 RAM 和 `uint32_t` 运行和，不在 sensorTask 栈上分配快照数组；单路最大和仅为 `5 × 4095 = 20475`。
- 电机 `MOTOR_I` 参与堵转保护，因此不使用五周期均值；电机任务每 10 ms 读取一次瞬时电流，`AT+SENSE?` 仍只按 1 Hz 快照节奏发布该状态值。

### 器件 NTC 过温停机

- 保护使用 MCU_C、LM51770_C、MP4317_C、DRV8874_C 和 CHARGE_MOS_C；BATT_NTC 仅显示。七路采样全部成功时，保护输入与 `AT+SENSE?` 发布值一致。若电池 NTC 或电池电压读取失败而五路受保护器件通道成功，保护使用正式同步窗口加本周期器件样本计算只读预览均值，但不推进窗口、不发布 SENSE。
- 任一路严格高于 60.0°C、温度转换无效，或受保护 NTC 的 ADC 读取失败，都会锁存过温保护。触发后取消 CHARGE/DRIVE，关闭 LM51770、MP4317、NMOS1、NMOS2，并让电机进入 SLEEP；LED 保持原状态。
- 停机请求通过高优先级队列发送，输出任务和电机任务还会每 100 ms 复查保护状态，阻止停机前已排队的旧开启命令重新打开输出。
- 只有五路受保护器件温度都有效且全部不高于 55.0°C 才解除锁存。解除后仅重新允许人工命令，不自动恢复停机前的充电、驱动、NMOS 或电机状态。
- 保护期间仍允许 OFF、MOTOR SLEEP、查询以及 `CHARGE_TIME` 设置/查询；CHARGE、DRIVE、NMOS1/2 ON 和非 SLEEP 电机模式返回 `ERROR:OVER_TEMPERATURE`。v3.2 没有新增温度阈值调参指令。

### 电机控制说明

当前电机逻辑遵循 DRV8874 的 PH/EN 使用方式：

- nSLEEP 低电平时驱动休眠。
- EN/IN1 为使能，PH/IN2 为方向。
- 从睡眠切到运行前会先 wake，再设置方向和使能。
- 正反转切换前会先制动，并加入 20ms 死区。
- IPROPI 下拉电阻 R19 为 220 Ω，AIPROPI 按 450 µA/A 计算：`I_mA = V_mV × 1000 / 99`；3.3 V ADC 满量程约为 33.3 A。
- 电机任务每 10 ms 采样。每次 FWD/REV 后先屏蔽 300 ms 启动浪涌；之后电流连续不低于阈值 100 ms 才判定堵转，立即关闭 EN、保持 nSLEEP 和方向、进入 BRAKE，并把 `OVERCURRENT` 锁存为 1。
- 默认阈值为 4000 mA，可用 `AT+STALL_CURRENT=<1000..30000>` 设置。成功写入 STM32 最后一页 1 KiB Flash，并带 magic、版本与 CRC32 校验；记录无效时启动回退到 4000 mA。
- 堵转后允许再次发送 FWD 或 REV，两方向均可启动且会清除旧锁存、重新执行 300 ms 屏蔽；若机械堵塞仍存在，屏蔽结束后会再次快速制动。SLEEP/WAKE/BRAKE/STOP 不清除锁存。
- 为避免运行中擦写 Flash，FWD/REV 时设置阈值返回 `ERROR:MOTOR_RUNNING`；设置相同值不重复擦写。查询命令为 `AT+STALL_CURRENT=?`，响应为 `+STALL_CURRENT:<mA>` 后跟 `OK`。
- 该软件停机不能替代 DRV8874 的硬件过流/故障保护、保险与功率设计。验证使用较低阈值或可控负载，不应故意制造理论 19 A 的硬堵转。

## 架构分层

当前工程按“平台层”和“业务层”拆分，目的不是为了追求目录好看，而是为了把 CubeMX 生成代码和手写业务逻辑隔离开。

### Core 层

[Core](Core) 代表平台层，主要放这些内容：

- STM32CubeMX 生成的初始化代码
- 启动流程、时钟配置、中断入口
- GPIO、ADC、USART、FreeRTOS 接入等底层胶水代码

这一层的原则是：

- 尽量保持贴近 CubeMX 默认结构
- 允许少量用户代码接入业务入口
- 不把复杂业务逻辑继续堆在这里

### App 层

[App](App) 代表业务层，主要放这些内容：

- AT 协议解析与命令分发
- UART2/UART3 双向隧道编码与桥接任务
- 传感采样任务
- 电机控制任务
- LED 与 NMOS 控制任务
- 共享状态与运行时封装

这一层的原则是：

- 按职责拆分模块，而不是把所有逻辑塞进单个文件
- 让业务代码尽量不感知 CubeMX 生成细节
- 后续新增功能优先落在 App，而不是继续回流到 Core

### 依赖方向

建议按下面的依赖关系理解这个工程：

```text
App 业务层
   ↓
Core 平台接入层
   ↓
HAL / CMSIS / FreeRTOS / Startup
```

换句话说：

- App 可以调用 Core 暴露出来的底层能力和 HAL 接口
- Core 不应该反向依赖 App 的内部实现细节
- `main.c`、`freertos.c` 这类入口文件只负责把 App 挂起来，不负责承载具体业务

## 项目结构

从目录上看，可以把这个仓库理解成“Core 是平台，App 是业务，Drivers/Middlewares 是第三方依赖”。

```text
stem-hub/
├─ Core/
│  ├─ Inc/
│  │  ├─ main.h                 # GPIO 宏定义
│  │  └─ ...
│  └─ Src/
│     ├─ freertos.c             # 任务创建和 RTOS 接入
│     ├─ adc.c                  # ADC 初始化
│     ├─ usart.c                # UART 初始化与中断配置
│     ├─ stm32f1xx_it.c         # 中断处理函数
│     └─ main.c                 # 系统入口与上电安全态
├─ App/
│  ├─ Inc/                      # 业务层头文件，对外模块接口
│  └─ Src/                      # 业务层实现文件，包含 AT、隧道、采样和控制任务
├─ Drivers/                     # STM32 HAL / CMSIS
├─ Middlewares/                 # FreeRTOS
├─ cmake/                       # 工具链和 CubeMX CMake 配置
├─ tests/
│  ├─ test_at_protocol.c        # 主机侧 AT 协议解析测试
│  └─ test_uart_tunnel.c        # UART 异步事件编码测试
├─ CMakeLists.txt
├─ CMakePresets.json
├─ stem-hub.ioc
└─ 钻杆mcu控制功能.md            # 需求说明
```

## 配置说明

### 主要引脚

| 功能 | 引脚 |
| --- | --- |
| LED1 | PC15 |
| LED3 | PA15 |
| NMOS1 | PA12 |
| NMOS2 | PB4 |
| MP4317 (NMOS 控制) | PA8 |
| EN/UVLO | PB3 |
| EN/IN1 | PB12 |
| PH/IN2 | PB13 |
| nSLEEP | PB14 |
| nFAULT | PB15 |
| nFLT | PA11 |

### UART 引脚

| UART | TX | RX |
| --- | --- | --- |
| USART1 | PA9 | PA10 |
| USART2 | PA2 | PA3 |
| USART3 | PB10 | PB11 |

### 可调整参数

以下参数目前主要在 [App/Inc/app_config.h](App/Inc/app_config.h) 中定义，可按需求调整：

- UART1 环形缓冲大小
- UART2/UART3 接收环形缓冲大小
- 二进制隧道单帧字节数（当前固定上限 32 字节）
- AT 指令最大长度（当前 96 字节）
- 传感采样周期
- 电机 wake 延时
- 正反转切换死区
- 过流阈值
- ADC 超时时间

## 故障排查

### 1. cmake 命令找不到

原因通常是 CMake 未安装，或安装后没有加入 PATH。

处理方式：

- 重新安装 CMake
- 勾选 Add CMake to the system PATH
- 重开终端后再次执行 cmake --version

### 2. arm-none-eabi-gcc 命令找不到

原因通常是 GNU Arm 工具链未加入 PATH。

处理方式：

- 安装 GNU Arm Embedded Toolchain
- 将其 bin 目录加入 PATH
- 重新打开终端后执行 arm-none-eabi-gcc --version

### 3. AT+SENSE? 返回 ERROR:SENSE_NOT_READY

说明传感任务还没有完成第一次有效采样。

处理方式：

- 上电后等待至少 1 秒再查询
- 检查 ADC 初始化和硬件接线

### 4. UART 隧道没有数据或返回错误

常见原因：

- 对应桥接没有打开；此时 `AT+UARTTX` 返回 `ERROR:UART_DISABLED`
- `<HEX>` 为空、长度为奇数、含小写或非十六进制字符；此时返回 `ERROR:HEX`
- 单帧超过 32 字节；上位机应拆分后逐帧等待 `OK`
- UART2/UART3 实际发送失败；此时返回 `ERROR:UART_TX`
- UART2/UART3 串口线连接错误

优先确认：

```text
AT+UART2=ON
AT+UARTTX=414243
```

### 5. 电机不转或上电即异常

优先检查：

- nSLEEP 是否被正确拉高
- EN/IN1 与 PH/IN2 接线是否对应
- 充电路径是否已用 `AT+CHARGE=ON` 打开，或后级驱动路径是否已用 `AT+DRIVE=ON` 打开
- 电机电流检测是否异常导致被误判过流

## 参与开发

如果你准备继续扩展这个项目，建议从下面几项开始：

1. 为 UART2/UART3 长时间满速接收、缓冲溢出和线路错误恢复补充实机压力测试。
2. 为电机、电源和故障状态增加更多主机侧测试。
3. 评估 DMA 串口接收或 ADC 扫描采样方案，但要先确认复杂度和 RAM 余量是否可接受。
4. 若通过 CubeMX 重新生成代码，务必检查用户代码区和 [cmake/stm32cubemx/CMakeLists.txt](cmake/stm32cubemx/CMakeLists.txt) 是否仍然保留了应用层源文件。
5. 新增任务、队列或缓冲区时重新检查 20 KB RAM 占用和任务栈高水位；七路滤波窗口位于静态 RAM，充电循环未新增任务栈。
6. 新增业务模块时，优先放进 [App/Inc](App/Inc) 和 [App/Src](App/Src)，避免把业务重新写回 [Core](Core)。

## 相关文件

- [钻杆mcu控制功能.md](钻杆mcu控制功能.md)：原始需求说明
- [motor_driver_ph_en_mode_prompt.md](motor_driver_ph_en_mode_prompt.md)：DRV8874 PH/EN 模式参考
- [上位机AT命令文档.md](上位机AT命令文档.md)：上位机联调用的 AT 命令与回包说明
- [App/Src/app_runtime.c](App/Src/app_runtime.c)：共享运行时、对象创建和 UART 回调
- [App/Src/app_state.c](App/Src/app_state.c)：共享业务状态访问层
- [App/Src/app_at_task.c](App/Src/app_at_task.c)：AT 命令执行与透传逻辑
- [App/Src/app_at_protocol.c](App/Src/app_at_protocol.c)：AT 指令解析实现
- [App/Src/app_bridge_task.c](App/Src/app_bridge_task.c)：UART2/UART3 接收缓冲排空与异步事件发送
- [App/Src/app_uart_tunnel.c](App/Src/app_uart_tunnel.c)：二进制数据到大写十六进制事件帧的编码
- [tests/test_at_protocol.c](tests/test_at_protocol.c)：AT 协议测试
- [tests/test_uart_tunnel.c](tests/test_uart_tunnel.c)：隧道事件编码测试
