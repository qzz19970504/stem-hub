# release-v3.3 输出旁路实机测试记录（2026-08-25）

## 测试对象

- 固件分支：`codex/output-bypass-v3.3`
- 固件功能提交：`5794b8e feat: add v3.3 output state and interlocks`
- 上位机分支：`codex/bypass-controls-ui`
- 上位机功能提交：`1e3ee16 feat: add confirmed bypass controls UI`
- Release ELF：`build/Release/stem-hub.elf`
- ELF SHA-256：`26B18486F9B74A03B5A330D8D4E0C1D0C8B1EDCC31074B44C62DA2FE4AD0612F`
- STM32CubeProgrammer：v2.23.0
- ST-Link：`37FF71064E573436947D1143`，V2J47S7，目标电压 3.26 V
- 目标：STM32F101/F102/F103 Medium-density，Device ID `0x410`，64 KiB NVM
- 串口：`COM12`，FTDI VID:PID `0403:6001`，序列号 `BG03F6XAA`，9600 8N1

## 下载与校验

使用 SWD under-reset 下载明确选择的 Release ELF。STM32CubeProgrammer 报告文件大小 36.50 KiB、起始地址 `0x08000000`、`Download verified successfully`，随后执行 MCU software reset。未操作 option bytes，未执行无关整片擦除。

## 协议与联锁结果

1. `AT+VERSION?` 返回 `+VERSION:release-v3.3` 后跟 `OK`。
2. 安全初态 `AT+OUTPUT?` 返回：

   ```text
   +OUTPUT:POWER=OFF,CHARGE_PHASE=IDLE,NMOS1=0,NMOS2=0,LIGHTS=0,MOTOR_BYPASS=0,CHARGE_BYPASS=0
   OK
   ```

3. OFF 状态下 `AT+NMOS1=ON`、`AT+NMOS2=ON`、`AT+LED=ON`、`AT+CHARGE_BYPASS=ON` 和 `AT+MOTOR_BYPASS=ON` 均返回 `ERROR:STATE`。
4. `AT+DRIVE=ON` 后 NMOS1、NMOS2、LIGHTS 均可开启；回读三项为 1。`AT+DRIVE=OFF` 后完整回读自动恢复 `POWER=OFF` 且三项为 0。
5. `AT+CHARGE=ON`、`AT+CHARGE_BYPASS=ON` 成功。12 秒后回读：

   ```text
   +OUTPUT:POWER=CHARGE,CHARGE_PHASE=OFF,NMOS1=0,NMOS2=0,LIGHTS=0,MOTOR_BYPASS=0,CHARGE_BYPASS=1
   OK
   ```

6. 在上述周期 OFF 状态下，以 ST-Link Hot Plug 读取 GPIOC ODR 地址 `0x4001100C`，结果为 `0x00004000`；bit 14 为 1，确认 PC14 跨周期 OFF 保持高电平。读取使用 Hot Plug，未复位目标。
7. STOP 状态下 MOTOR BYPASS ON 返回 `ERROR:STATE`；FWD 后可开启并回读 1；切换 REV 自动回读 0；REV 中重新开启后执行 STOP，再次自动回读 0。

## 上位机真实串口结果

使用功能分支源码、真实 `COM12` 和离屏 Qt 窗口完成端到端控件测试：

- v3.3 握手和首轮 OUTPUT 回读成功。
- OFF 时 CHARGE/DRIVE 可用，CHARGE BYPASS 和三个 DRIVE 子项灰显。
- DRIVE 确认后子项启用；NMOS1、LIGHTS 的点亮与固件回读一致。
- 退出 DRIVE 后子项自动熄灭并灰显。
- 在 OFF 状态直接向控制器请求 NMOS1 ON，固件拒绝后 UI 恢复最后确认的关闭状态。
- CHARGE 确认后 CHARGE BYPASS 启用并正确点亮。
- FWD/REV 时 MOTOR BYPASS 可用；换向后固件回读清零，UI 随确认状态熄灭。
- 最终清理回读 `POWER=OFF`，NMOS1、NMOS2、LIGHTS、MOTOR_BYPASS、CHARGE_BYPASS 全部为 0，电机模式 STOP。

前两次 UI 自动化试跑分别因首轮 OUTPUT 尚未轮询到、以及换向后在 100 ms UI 刷新前过早断言而退出；两次 `finally` 安全清理均成功。改为等待确认状态/控件条件后，完整序列通过。

## 结论

release-v3.3 的 OUTPUT 查询、CHARGE 旁路跨 OFF 锁存、DRIVE 子项固件联锁、PC13 自动清除和上位机确认状态门控均通过本次实机验证。
