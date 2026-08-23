# PC13/PC14 限流电阻旁路实机测试（2026-08-23）

## 测试环境

- 目标：STM32F103C8T6（CubeProgrammer 识别为 STM32F101/F102/F103 Medium-density，64 KiB）
- ST-Link：`37FF71064E573436947D1143`，目标电压 3.26 V，SWD under reset 烧录
- 固件：`build/Release/stem-hub.elf`，大小 35.88 KiB
- 烧录结果：`Download verified successfully`，随后 MCU reset
- 控制串口：FTDI `COM12`，9600 8N1
- GPIOC ODR：`0x4001100C`，使用 ST-Link Hot Plug 只读；bit 13/14 分别对应 PC13/PC14

## 无负载互锁

先强制 `MOTOR=STOP`、`CHARGE=OFF`，再执行默认测试序列：

| 操作 | 实际响应 |
| --- | --- |
| `AT+VERSION?` | `+VERSION:release-v3.2` 后跟 `OK` |
| `AT+MOTOR_BYPASS=OFF` | `OK` |
| 停机状态 `AT+MOTOR_BYPASS=ON` | `ERROR:STATE` |
| `AT+CHARGE_BYPASS=OFF` | `OK` |
| 非充电状态 `AT+CHARGE_BYPASS=ON` | `ERROR:STATE` |

脚本的 `finally` 清理四条命令均返回 `OK`。

## 短时带载与引脚观测

使用 `--exercise-loads --settle-seconds 0.5 --hold-seconds 5`。未制造堵转或过温故障。

| 相对时间 | 动作/状态 | PC13 | PC14 |
| --- | --- | ---: | ---: |
| 0.2 s | 初始安全状态 | 0 | 0 |
| 1.0 s | FWD 启动后执行 `MOTOR_BYPASS=ON` | 1 | 0 |
| 6.2 s | FWD→REV 换向，尚未重新 ON | 0 | 0 |
| 6.7 s | REV 启动后再次执行 `MOTOR_BYPASS=ON` | 1 | 0 |
| 11.7 s | 电机旁路 OFF、MOTOR STOP | 0 | 0 |
| 12.3 s | CHARGE 实际 ON 后执行 `CHARGE_BYPASS=ON` | 0 | 1 |
| 17.4 s | `CHARGE=OFF` 与最终清理 | 0 | 0 |

所有带载 AT 命令返回 `OK`，测试脚本退出码为 0。读数同时证明：PC13 在换向时自动取消，且新方向必须重新 ON；PC14 在退出充电时自动取消；最终两个旁路均回到默认低电平。

## 覆盖边界

- 已实机覆盖烧录校验、9600 握手、非法状态拒绝、FWD/REV、换向自动取消、充电 ON、退出充电自动取消和最终安全清理。
- 周期自然进入 OFF、堵转停机和过温停机的自动拉低路径由原生策略测试与源码所有权契约覆盖；本次为避免不必要的功率与热应力，没有故意制造堵转或过温，也没有等待完整 60 秒周期。
