# LM51770 间歇充电保护设计

## 背景与目标

连续启用 LM51770 约 60 秒曾导致功率级过热并永久损坏。当前硬件没有靠近
LM51770 或功率 MOSFET 的可信温度测点，因此本次只实施固定时间的临时降额：
充电路径开启 10 秒、关闭 50 秒并自动循环。

该策略只限制 EN/UVLO 的导通时间，不证明功率级在真实负载下安全。后续仍需
检查充电电流、限流设置、MOSFET、电感饱和、环路、布局和散热。

## 外部行为

- `AT+CHARGE=ON`：若尚未处于充电循环，立即执行安全的全关后仅打开
  LM51770，并启动 10 秒 ON 相位。
- ON 相位到期：关闭 LM51770，进入 50 秒 OFF 相位。
- OFF 相位到期：先关闭两路，再仅打开 LM51770，重新进入 ON 相位。
- 循环中的重复 `AT+CHARGE=ON` 是幂等操作，不改变当前相位或截止时间。
- `AT+CHARGE=OFF`、`AT+DRIVE=OFF`、`AT+POWER=OFF` 立即取消循环并全关。
- `AT+DRIVE=ON` 立即取消循环，先全关，再仅打开 MP4317。
- 复位后保持两路全关，不恢复之前的循环。
- AT 命令集不变；`OK` 仍表示请求成功入队，而不是当前物理 EN 必然为开启。

## 内部架构

新增平台无关的 `app_charge_cycle` 模块，保存 `IDLE`、`ON_PHASE`、
`OFF_PHASE`、绝对截止 Tick，以及固定的 ON/OFF Tick 数。模块只返回是否
需要施加一个 `AppPowerMode`，不直接访问 GPIO、HAL、FreeRTOS 或共享状态。

`nmosTask` 继续唯一持有 PB3/PA8。任务每轮先处理已经到期的相位，再按绝对
deadline 计算 `osMessageQueueGet` 的剩余超时。无关 NMOS 消息不会重置计时。
所有物理切换仍调用 `App_PowerPathApply()`，保持“两路先关，再最多开一路”。

时间常量固定为：

- `APP_CHARGE_ON_TIME_MS = 10000`
- `APP_CHARGE_OFF_TIME_MS = 50000`

固件版本升级为 `release-v3.1`。不增加 NTC、充电电流、累计时间、自动锁存、
运行时调参或相位查询。

## 验证与安全边界

本机测试覆盖正常循环、重复 ON 幂等、命令打断、无关消息不延时和 Tick 回绕。
固件执行完整 native 测试与 clean Debug 构建。

实机只在功率级隔离或禁止带载的条件下，通过 COM12 发送命令，并用 ST-Link
HOTPLUG 读取 GPIOA/GPIOB ODR 验证 PA8/PB3 时序。不执行真实充电测试。

上位机代码和 UI 不变；CHARGE 开关表示循环已启用，不表示当前处于 ON 相位。
仅同步当前协议文档。
