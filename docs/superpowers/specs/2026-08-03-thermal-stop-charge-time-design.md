# NTC 过温停机与可调充电时间设计

## 目标与范围

本测试分支在不修改固件版本号、不修改上位机仓库的前提下增加两项实验功能：

1. NTC1、NTC2、NTC3 任一路温度超过 60.0°C 时自动停机。
2. 通过 UART1 AT 指令设置并查询 60 秒间歇充电周期中的开启时间。

本分支只保留供测试，不合并回 `master`。电池 NTC 不参与保护，因为当前硬件没有焊接该传感器。LED 不参与过温停机。

## 过温保护语义

- sensorTask 每秒完成一次有效采样周期后，使用与 `AT+SENSE?` 相同的五周期滑动平均温度检查 NTC1、NTC2、NTC3。
- 任一路有效温度严格大于 60.0°C，或任一路转换结果为 `ERR`，立即进入过温保护。
- 进入保护时：
  - CHARGE/DRIVE 电源路径全关，并取消间歇充电循环；
  - NMOS1、NMOS2 关闭；
  - DRV8874 进入 SLEEP；
  - LED 状态保持不变。
- 保护期间，AT 层拒绝 CHARGE、DRIVE、NMOS1/2 ON 和除 SLEEP 外的电机模式，返回 `ERROR:OVER_TEMPERATURE`。OFF、SLEEP、查询和 `CHARGE_TIME` 指令仍可使用。
- nmosTask 与 motorTask 在执行请求前再次检查保护状态，避免过温前已排队的旧启动命令在停机后重新开启输出。
- 只有三个 NTC 均为有效读数且都不高于 55.0°C 时才解除保护。解除只恢复接受人工命令，不自动恢复停机前状态。
- MCU 复位后保护状态从未触发开始；第一次有效采样若已过温，会立即触发停机。

## 任务边界与停机路径

不新增 FreeRTOS 任务、定时器或任务栈。

- 新建纯 C `app_thermal_guard` 状态机，负责 60.0°C 触发、55.0°C 解除和错误读数处理。
- app_state 保存线程安全的 `thermal_protection_active` 与 `charge_on_time_seconds`。
- sensorTask 是温度判断入口。保护状态先写入 app_state，再向输出队列和电机队列发送高优先级停机请求。
- 输出队列增加单个热停机请求；nmosTask 原子执行“取消充电循环、关闭 LM51770/MP4317、关闭 NMOS1/2”。
- motorTask 接收高优先级 SLEEP 请求。安全请求使用可等待的队列写入，避免队列暂满时静默丢失。
- GPIO 仍只由原有 nmosTask/motorTask 持有，sensorTask 不直接写 GPIO。

## CHARGE_TIME 协议

UART1 新增两条 ASCII 指令：

```text
AT+CHARGE_TIME=n\r\n
AT+CHARGE_TIME=?\r\n
```

- `n` 必须是十进制整数 1 到 60；非法、缺失或越界值返回 `ERROR:PARSE`。
- 设置成功返回 `OK`。
- 查询返回：

  ```text
  +CHARGE_TIME:<n>\r\n
  OK\r\n
  ```

- 一个充电周期固定为 60 秒，ON 为 `n` 秒，OFF 为 `60-n` 秒。
- 默认 `n=10`，只保存在 RAM；MCU 复位后恢复 10 秒 ON / 50 秒 OFF。
- 设置命令立即更新可查询配置，但不修改当前 ON/OFF 阶段的绝对截止 Tick；从下一次 ON 阶段开始对整个新周期使用新配置。
- `n=60` 表示连续充电。调度器仍以 60 秒为内部周期边界接收后续配置，但边界处不切换 EN，避免 LM51770 每分钟重新软启动。
- 充电循环继续使用 32 位有符号 Tick 差处理计数回绕。重复 `AT+CHARGE=ON` 不重置当前阶段。

## 兼容性

- `APP_FIRMWARE_VERSION` 保持 `release-v3.1`，握手行为不变。
- 不修改上位机仓库、UI、fake firmware 或打包产物。
- 旧的 `AT+CHARGE=ON/OFF`、`AT+DRIVE=ON/OFF`、`AT+POWER=OFF` 行为保持兼容；仅 ON 时长改为读取 RAM 配置。
- 相关 MCU README、AT 文档和功能说明注明这是测试分支能力。

## 测试策略

### 本机自动测试

- AT 解析：设置 1、10、60；查询；拒绝 0、61、负数、小数、空值和多余字符。
- 充电调度：默认 10/50；1/59；59/1；60 秒连续；运行中修改不改变当前 deadline；下一 ON 周期应用新值；重复 CHARGE 不重置；OFF/DRIVE 取消；Tick 回绕。
- 热保护：任一路 >60.0°C 触发；恰好 60.0°C 不触发；错误值触发；触发后 55.1°C 不解除；三路均 <=55.0°C 才解除；解除不产生自动恢复动作。
- 安全策略：保护期间拒绝所有开启动作，允许关闭、SLEEP、查询和时间设置。
- 停机输出：记录 LM51770/MP4317 全关、NMOS1/2 关闭和 motor SLEEP 请求。
- 运行全部 MCU 本机测试并 clean Debug 构建，检查 RAM/Flash 与任务栈配置。

### 实机验证

- 通过实际枚举的 UART1 COM 口确认版本仍为 `release-v3.1`。
- 查询默认值为 10；设置 1 后查询为 1，并用 ST-Link HOTPLUG 读取 PA8/PB3，验证约 1 秒 ON、59 秒 OFF。
- 设置 60 后验证跨过 60 秒仍保持 LM51770 ON，没有周期边界重启。
- 设置非法值确认 `ERROR:PARSE`。
- 过温算法和停机顺序由本机测试覆盖；若现场无法安全加热 NTC，则不伪造带载过温结论。
- 测试结束发送 `AT+POWER=OFF`、关闭 NMOS1/2 并让电机 SLEEP，确认安全 GPIO 状态。

## 安全边界

该功能是测试分支的软件保护，不替代硬件过温、过流、充电时长限制或功率器件设计验证。五周期平均会引入最多约数秒的响应延迟；正式安全版本应评估独立硬件保护、瞬时温度路径、传感器开短路诊断和故障持久化。
