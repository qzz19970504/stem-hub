# stem-hub

基于 STM32F103C8T6 和 FreeRTOS 的嵌入式控制项目，当前聚焦于多路 ADC 采集、DRV8874 电机驱动、UART AT 指令控制，以及 UART1 与 UART2/UART3 之间的可控双向二进制隧道。

当前固件版本为 `release-v3.3`。本版在 v3.2 语义化温度遥测基础上增加完整输出状态查询、CHARGE 模式旁路锁存，以及 DRIVE 子输出固件联锁。

## 产品文档

README 是仓库的开发入口；产品接入、协议细节和现场操作请使用以下独立文档。

| 读者与目的 | 文档 |
| --- | --- |
| 上位机开发、系统集成 | [stem-hub模块集成与使用说明.md](stem-hub模块集成与使用说明.md) |
| 精确 AT 命令、响应和错误码 | [上位机AT命令文档.md](上位机AT命令文档.md) |
| 现场运行与故障处置 | [stem-hub模块操作说明.md](stem-hub模块操作说明.md) |

推荐阅读顺序：初次集成先阅读主文档；需要精确报文时查 AT 文档；现场人员直接使用操作说明。

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
- UART2、UART3 支持按控制状态独立开关，并提供受控的双向二进制隧道。
- 保留兼容旧上位机的行文本转发路径；新上位机的帧协议以 AT 文档为准。
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

> **开发/调试提示，不构成对外硬件产品规格。** 供电、电平、连接器、机械和环境要求以独立硬件规格书为准。

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

## 固件通信烟雾测试

UART1 使用 9600 8N1、无流控。连接调试串口后，发送：

```text
AT+VERSION?\r\n
```

正常情况下应收到 `+VERSION:release-v3.3`，后跟 `OK`。完整接入流程、AT 协议和现场操作分别见上方三份产品文档。

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

> **仅用于当前仓库开发/调试，不构成对外产品规格。** 以下主要引脚和 UART 引脚信息的供电、电平、连接器、机械和环境要求以独立硬件规格书为准。

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

### 3. 串口通信异常

优先确认 UART1 的 9600 8N1 配置、收发引脚和共地；再按上方 AT 文档核对命令、响应及错误码。UART2/UART3 隧道还应检查对应串口的接线和目标端状态。

### 4. 电机不转或上电即异常

优先检查：

- nSLEEP 是否被正确拉高
- EN/IN1 与 PH/IN2 接线是否对应
- 电源路径是否已按产品文档进入正确的运行状态
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
- [stem-hub模块集成与使用说明.md](stem-hub模块集成与使用说明.md)：上位机与系统集成主文档
- [上位机AT命令文档.md](上位机AT命令文档.md)：精确的 AT 命令、响应和错误码
- [stem-hub模块操作说明.md](stem-hub模块操作说明.md)：现场运行与故障处置
- [CMakeLists.txt](CMakeLists.txt)：工程顶层构建入口
- [CMakePresets.json](CMakePresets.json)：Debug 与 Release 构建预设
- [App/Src/app_runtime.c](App/Src/app_runtime.c)：共享运行时、对象创建和 UART 回调
- [App/Src/app_state.c](App/Src/app_state.c)：共享业务状态访问层
- [App/Src/app_at_task.c](App/Src/app_at_task.c)：AT 命令执行与透传逻辑
- [App/Src/app_at_protocol.c](App/Src/app_at_protocol.c)：AT 指令解析实现
- [App/Src/app_bridge_task.c](App/Src/app_bridge_task.c)：UART2/UART3 接收缓冲排空与异步事件发送
- [App/Src/app_uart_tunnel.c](App/Src/app_uart_tunnel.c)：二进制数据到大写十六进制事件帧的编码
- [tests/test_at_protocol.c](tests/test_at_protocol.c)：AT 协议测试
- [tests/test_uart_tunnel.c](tests/test_uart_tunnel.c)：隧道事件编码测试
