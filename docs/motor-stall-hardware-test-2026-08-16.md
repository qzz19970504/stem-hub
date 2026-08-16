# 电机堵转保护硬件测试记录（2026-08-16）

## 测试环境

- 固件分支：`codex/motor-stall-protection`
- 固件版本握手：`release-v3.2`
- MCU：STM32F103C8T6，CubeProgrammer 识别为 64 KiB Flash
- ST-Link：`37FF71064E573436947D1143`，目标电压 3.26 V
- RS-485：FTDI `COM12`，115200 8N1
- 烧录镜像：功能 worktree 的 `build/Debug/stem-hub.elf`

## 自动化与构建

- 17 个本机 C 测试全部编译并退出 0。
- 5 个 Python 源码契约测试全部通过。
- Debug 构建：Flash 59,528 B / 63 KiB，RAM 19,432 B / 20 KiB。
- Release 构建：Flash 36,052 B / 63 KiB，RAM 19,424 B / 20 KiB。
- 链接脚本已把最后 1 KiB Flash 页排除在固件镜像外。

## 烧录与基础通信

CubeProgrammer 完成下载、读回校验和复位，输出 `Download verified successfully`。复位后 RS-485 返回：

```text
AT+VERSION?
+VERSION:release-v3.2
OK

AT+STALL_CURRENT=?
+STALL_CURRENT:4000
OK

AT+MOTOR?
+MOTOR:MODE=SLEEP,CURRENT_MA=0,OVERCURRENT=0,FAULT=0
OK
```

## 阈值持久化

1. 空闲时发送 `AT+STALL_CURRENT=4100`，返回 `OK`。
2. 查询返回 `+STALL_CURRENT:4100`。
3. 通过 ST-Link 硬件连接执行 MCU reset，不重新写最后一页。
4. 复位后查询仍返回 `+STALL_CURRENT:4100`，证明非易失加载有效。
5. 设置回 4000 mA，再次复位后查询为 `+STALL_CURRENT:4000`。

## 运行中写保护

短时发送 FWD 后尝试修改阈值：

```text
AT+MOTOR=FWD
OK
AT+STALL_CURRENT=4200
ERROR:MOTOR_RUNNING
AT+STALL_CURRENT=?
+STALL_CURRENT:4000
OK
AT+MOTOR=BRAKE
OK
```

阈值未改变，随后主动 BRAKE。

## 堵转停机实测状态

为避免理论 19 A 硬堵转，使用最低合法阈值 1000 mA 做受控运行，计划验证超过阈值后自动 BRAKE。约 1.5 秒内连续 15 次 `AT+MOTOR?` 均为：

```text
+MOTOR:MODE=FWD,CURRENT_MA=0,OVERCURRENT=0,FAULT=0
OK
```

因此本轮没有形成可用于堵转判定的实机电流，不能把自动停机硬件链路记为通过。只读诊断同时显示：

```text
+SENSE:BATT_V=2.8V,...,MOTOR_I=0.0A,...
+FAULT:DRV=0,AUX=0
+DIAG:...SENSOR_ADC1_READ_FAIL=0,SENSOR_ADC2_READ_FAIL=0...
```

ADC 采集与发布没有报错，但电机功率侧/负载当前未形成非零 IPROPI 电流。需要在电机电源正常、机械负载可控的条件下重测：先确认空载约 0.5 A，再把阈值设为略高于空载且低于受控负载电流，验证 300 ms 屏蔽、连续 100 ms 超限、BRAKE 和 `OVERCURRENT=1`，之后验证 FWD/REV 均能重新启动且堵塞方向再次停机。

## 测试结束状态

- 电机模式：BRAKE
- 持久化阈值：4000 mA
- DRV/AUX 故障：0/0
- 未执行 19 A 硬堵转
- 自动停机硬件链路：等待电机功率与可控负载条件就绪后补测
