# stem-hub Product Documentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将现有固件交接资料重构为以集成工程师为主的产品使用说明、权威 AT 协议参考和独立现场操作说明，并确保三份文档与 `release-v3.3` 固件事实一致。

**Architecture:** 采用三层文档体系：主文档负责产品能力、状态模型、工作流和主机集成规则；AT 文档唯一负责命令语法、响应与错误码；操作文档只负责现场动作和故障处置。README 保留仓库开发入口，但删除重复协议事实，统一导航到三份产品文档。

**Tech Stack:** Markdown、PowerShell、ripgrep、Git、C11 主机侧协议测试、Python/pytest 契约测试、CMake/Ninja STM32 Debug 构建。

---

## File Structure

| 文件 | 动作 | 单一职责 |
| --- | --- | --- |
| `stem-hub模块集成与使用说明.md` | 新建 | 产品能力、快速接入、业务工作流、主机集成、安全恢复和验收 |
| `设备固件与上位机集成交接文档.md` | 删除 | 旧的源码交接入口，由产品主文档替代 |
| `上位机AT命令文档.md` | 修改 | AT 命令语法、参数、同步/异步响应和错误码的唯一权威参考 |
| `stem-hub模块操作说明.md` | 新建 | 现场启动、充电、驱动、电机、停止和异常处置 |
| `README.md` | 修改 | 仓库快速开始、开发入口和三类读者的文档导航 |

固件源码、测试源码和硬件资料仅作为事实核对来源，本次不修改其行为或内容。

### Task 1: 建立产品主文档并移除旧交接入口

**Files:**
- Create: `stem-hub模块集成与使用说明.md`
- Delete: `设备固件与上位机集成交接文档.md`
- Reference: `docs/superpowers/specs/2026-08-31-stem-hub-product-documentation-design.md`
- Reference: `App/Inc/app_config.h`
- Reference: `App/Inc/app_at_protocol.h`
- Reference: `App/Src/app_at_protocol.c`
- Reference: `App/Src/app_at_task.c`
- Reference: `App/Src/app_state.c`
- Reference: `Core/Src/freertos.c`

- [ ] **Step 1: 记录主文档必须保持的固件事实**

运行：

```powershell
rg -n "APP_FIRMWARE_VERSION|APP_POWER_MODE_|APP_CHARGE_|APP_MOTOR_|APP_THERMAL_|STALL|UART2RX|UART3RX" App/Inc App/Src Core/Src --glob "*.h" --glob "*.c"
```

期望：能够定位 `release-v3.3`、OFF/CHARGE/DRIVE、电机状态、充电周期、温度保护、堵转保护和异步 UART 事件的实际定义。写文档时只陈述该输出和现有测试能够支持的事实。

- [ ] **Step 2: 用产品视角创建主文档框架**

使用 `apply_patch` 新建 `stem-hub模块集成与使用说明.md`，使用以下精确一级/二级结构：

```markdown
# stem-hub 模块集成与使用说明

> 适用固件：`release-v3.3`
> 主要读者：上位机与系统集成工程师
> 协议参考：[上位机 AT 命令文档](./上位机AT命令文档.md)
> 现场操作：[stem-hub 模块操作说明](./stem-hub模块操作说明.md)

## 1. 产品概述
## 2. 适用范围与责任边界
## 3. 五分钟快速接入
## 4. 产品功能与状态模型
## 5. 标准业务流程
## 6. 上位机集成规则
## 7. 安全保护与异常恢复
## 8. 联调、验收与故障排查
## 9. 兼容性与版本维护
## 10. 附录：固件运行机制简述
```

“适用范围与责任边界”必须明确：本说明不定义供电范围、连接器、物理电平、机械安装和环境等级，这些信息以后续独立硬件规格书为准。

- [ ] **Step 3: 写入产品能力、状态模型和快速接入闭环**

“产品概述”用外部能力描述 UART1 控制、UART2/UART3 下游通信、电源路径、电机、辅助输出、传感和保护，不列源码文件或 RTOS 对象。

“五分钟快速接入”必须给出以下可直接验证的顺序和成功现象：

```text
1. 以 9600、8N1、无流控打开 UART1。
2. 发送 AT+VERSION?\r\n。
3. 收到 +VERSION:release-v3.3\r\nOK\r\n 后判定协议版本匹配。
4. 依次查询 OUTPUT、MOTOR、CHARGE_TIME、STALL_CURRENT、FAULT 和 SENSE。
5. SENSE_NOT_READY 只延迟 SENSE 重试，不否定握手成功。
```

状态模型必须区分“请求电源模式”和“实际充电相位”：

- `POWER=OFF|CHARGE|DRIVE` 是请求模式；
- `CHARGE_PHASE=IDLE|ON|OFF` 是充电路径实际相位；
- 离开 DRIVE 自动关闭 NMOS1、NMOS2 和 LIGHTS；
- 复位后所有危险输出回到安全状态；
- `CHARGE_TIME` 仅存 RAM，`STALL_CURRENT` 持久化到 Flash。

- [ ] **Step 4: 按业务目标重写标准流程**

为以下六个流程分别写“前置条件 → 操作顺序 → 成功判据 → 失败处理”，且命令精确语法只链接到 AT 文档：

1. 建立连接并同步状态；
2. 进入和退出充电模式；
3. 进入和退出驱动模式；
4. 启动、调节、换向和停止电机；
5. 打开 UART2/UART3 并进行下游通信；
6. 安全停止并断开连接。

充电流程明确 `OK` 表示循环请求被接受，不表示 LM51770 此刻一定处于 ON 相位；驱动流程明确子输出只能在 DRIVE 下开启；电机换向流程明确先进入安全状态并等待固件完成换向控制，不让上位机直接推断引脚状态。

- [ ] **Step 5: 写入主机集成、安全恢复和验收规则**

“上位机集成规则”必须覆盖：单通道串行请求、同步终止响应、`+UART2RX`/`+UART3RX` 异步事件分流、请求超时歧义、`OK` 后状态回读、未知版本拒绝危险控制、断线/复位后的重新握手与全量同步。

“安全保护与异常恢复”必须覆盖：

- 五路受保护器件温度任一路无效或严格高于 60.0°C 时锁存保护；
- 全部受保护温度不高于 55.0°C 后自动解除锁存，但旧输出不自动恢复；
- 堵转后电机进入安全状态，恢复前查询电机状态和故障；
- `nFAULT`/`nFLT` 当前仅查询，不承诺自动恢复；
- 超时不能证明命令未执行，必须重新查询实际状态。

“联调、验收与故障排查”包含可勾选项目：版本握手、初态同步、三类电源状态、DRIVE 子项联锁、电机状态、异步事件分流、过温停机、堵转停机、断线重连和安全关机。

- [ ] **Step 6: 将内部任务说明压缩为外部行为附录**

附录只保留以下七类职责的简化映射：AT 接收解析、下游桥接、传感采样、电机控制、LED 控制、输出/电源控制和启动指示。说明控制请求可能排队、传感快照约 1 Hz 更新、异步事件可能穿插；不保留任务栈大小、RTOS 对象表、函数名和源码路径索引。

- [ ] **Step 7: 删除旧文档并检查主文档结构**

使用 `apply_patch` 删除 `设备固件与上位机集成交接文档.md`，然后运行：

```powershell
rg -n "^#{1,3} " -- "stem-hub模块集成与使用说明.md"
rg -n "RTOS 对象|任务栈|源码与验证资料索引|App/Src/|Core/Src/" -- "stem-hub模块集成与使用说明.md"
```

期望：第一个命令显示步骤 2 的十个章节和必要子章节；第二个命令无输出。

- [ ] **Step 8: 提交产品主文档**

```powershell
git add -- "stem-hub模块集成与使用说明.md"
git commit -m "docs: add stem-hub module integration guide"
```

期望：提交只包含新的产品主文档；旧交接文档在工作区中已不存在。

### Task 2: 将 AT 文档收敛为协议唯一权威参考

**Files:**
- Modify: `上位机AT命令文档.md`
- Reference: `App/Inc/app_at_protocol.h`
- Reference: `App/Src/app_at_protocol.c`
- Reference: `App/Src/app_at_task.c`
- Test: `tests/test_at_protocol.c`

- [ ] **Step 1: 更新文档定位和交叉导航**

将开头说明改为：本文档是 `release-v3.3` AT 控制面的精确协议参考；产品状态模型和完整业务流程链接到 `stem-hub模块集成与使用说明.md`；现场步骤链接到 `stem-hub模块操作说明.md`。

在“文档范围”明确本文件唯一负责：命令语法、参数范围、大小写与结束符、同步响应、异步事件、错误码和协议兼容写法。产品介绍、完整操作流程、RTOS 任务和硬件规格不在本文件展开。

- [ ] **Step 2: 清理协议内部重复和版本陈旧表述**

运行：

```powershell
rg -n "ERROR:PARSE|v3\.2|设备固件与上位机集成交接文档" -- "上位机AT命令文档.md"
```

执行以下确定性修订：

- 错误说明列表中 `ERROR:PARSE` 只保留一条完整定义；
- 将描述当前协议字段或当前命令行为的 `v3.2` 改成“当前版本”或 `v3.3`；
- 只有明确描述版本演进历史时才保留 `v3.2`，并在同一句写明它是历史版本；
- 所有旧交接文档链接替换为新的主文档链接。

- [ ] **Step 3: 保持完整命令和错误契约**

逐项对照 `AppAtCommandType`、解析器分支和 AT 任务响应，确认文档继续包含以下命令族：UART2/3 开关、UARTTX、LED、MOTOR、MOTOR_BYPASS、STALL_CURRENT、NMOS1/2、CHARGE、CHARGE_BYPASS、DRIVE、POWER=OFF、CHARGE_TIME、SENSE、FAULT、MOTOR 查询、OUTPUT、DIAG、VERSION。

逐项确认错误章节包含固件会返回的公开 `ERROR:<reason>`，包括解析、状态、队列、UART、Flash、格式化和不支持类型错误。不得把主文档中的恢复建议复制成第二套错误定义；每个错误定义后只保留一条简短的主机处理建议。

- [ ] **Step 4: 缩减重复的产品流程内容**

将“推荐联调顺序”保留为协议层最小烟雾测试：版本、状态查询、一个安全的开关/关闭闭环、异步 UART 事件。将完整 CHARGE/DRIVE/电机/安全退出流程改成到主文档相应章节的链接。

将“上位机实现建议”限制为解析器规则：串行请求、终止响应识别、异步事件分流、超时后不得假设未执行。UI 门控、产品状态机和保护恢复的详细策略只链接到主文档。

- [ ] **Step 5: 运行协议事实核验**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_at_protocol.c App/Src/app_at_protocol.c -o "$env:TEMP\test_at_protocol.exe"
& "$env:TEMP\test_at_protocol.exe"
rg -n "设备固件与上位机集成交接文档|v3\.2" -- "上位机AT命令文档.md"
```

期望：C 测试无输出且退出码为 0；旧文件名无匹配；`v3.2` 只在明确标注的历史说明中出现，若无历史说明则完全无匹配。

- [ ] **Step 6: 提交协议参考调整**

```powershell
git add -- "上位机AT命令文档.md"
git commit -m "docs: focus AT guide on protocol reference"
```

期望：提交只包含 AT 协议文档。

### Task 3: 新增面向现场人员的独立操作说明

**Files:**
- Create: `stem-hub模块操作说明.md`
- Reference: `stem-hub模块集成与使用说明.md`
- Reference: `上位机AT命令文档.md`

- [ ] **Step 1: 创建动作导向的操作手册结构**

使用 `apply_patch` 新建以下结构：

```markdown
# stem-hub 模块操作说明

> 适用固件：`release-v3.3`
> 主要读者：设备操作、现场测试和维护人员
> 集成说明：[stem-hub 模块集成与使用说明](./stem-hub模块集成与使用说明.md)
> 协议参考：[上位机 AT 命令文档](./上位机AT命令文档.md)

## 1. 操作前须知
## 2. 连接与启动确认
## 3. 充电操作
## 4. 驱动操作
## 5. 电机操作
## 6. 状态与故障提示
## 7. 异常处置
## 8. 停机与断开
## 9. 禁止事项
```

开头明确：本手册假设模块已由集成工程师正确安装并由上位机提供对应操作入口；供电、接线、物理接口和安装要求以硬件规格书为准。

- [ ] **Step 2: 为正常操作写可观察的成功判据**

每个操作章节都使用编号步骤，并在末尾写“确认结果”。至少包含：

- 启动：版本为 `release-v3.3`，状态能够刷新，初态为安全状态；
- 充电：进入 CHARGE 后以电源状态和充电相位判断，不以单次 `OK` 或瞬时输出判断；
- 驱动：先进入 DRIVE，再开启允许的辅助输出；
- 电机：启动前确认无保护，停止后确认模式为 SLEEP；
- 停机：先停电机，再关闭辅助输出和电源路径，确认 OFF/IDLE 后断开。

只在“无法通过上位机界面判断时”提供最小诊断查询示例；不复制完整 AT 命令表。

- [ ] **Step 3: 编写状态解释和异常处置**

使用“看到什么 → 代表什么 → 立即做什么 → 恢复条件”的格式覆盖：

- 版本不匹配；
- 状态长时间不刷新或通信中断；
- `SENSE_NOT_READY`；
- 过温保护；
- 电机堵转；
- `nFAULT` 或 `nFLT` 有效；
- 命令返回成功但状态未改变。

过温解除后禁止自动恢复旧输出；堵转和硬件故障必须先排除机械/负载原因再重新启动；通信中断时先执行安全停机策略，再由集成工程师重新建立连接。

- [ ] **Step 4: 写入禁止事项并扫描实现细节**

禁止事项至少包括：禁止跳过版本检查、禁止并发连续发送控制请求、禁止只凭 `OK` 更新最终状态、禁止保护解除后自动恢复危险输出、禁止无人值守带载、禁止将本软件说明当作硬件额定值依据。

运行：

```powershell
rg -n "RTOS|FreeRTOS|消息队列|事件组|任务栈|GPIO|ADC|App/Src/|Core/Src/|函数" -- "stem-hub模块操作说明.md"
```

期望：无输出。若某个底层术语对操作不可避免，改写为用户可观察的“状态”“保护”或“输出”。

- [ ] **Step 5: 提交操作说明**

```powershell
git add -- "stem-hub模块操作说明.md"
git commit -m "docs: add stem-hub operator guide"
```

期望：提交只包含新的操作说明。

### Task 4: 将 README 改为清晰入口并消除协议重复

**Files:**
- Modify: `README.md`
- Reference: `stem-hub模块集成与使用说明.md`
- Reference: `上位机AT命令文档.md`
- Reference: `stem-hub模块操作说明.md`

- [ ] **Step 1: 在 README 顶部加入三类读者导航**

在项目一句话和版本说明之后、编译“快速开始”之前加入“产品文档”表：

| 读者/目的 | 入口 |
| --- | --- |
| 上位机和系统集成 | `stem-hub模块集成与使用说明.md` |
| 查询 AT 命令、响应和错误码 | `上位机AT命令文档.md` |
| 现场运行与故障处置 | `stem-hub模块操作说明.md` |

注明推荐阅读顺序：初次集成先读主文档，需要精确报文时再查 AT 文档；现场人员直接阅读操作说明。

- [ ] **Step 2: 将 README 的重复协议内容替换为最小烟雾测试**

保留仓库编译、开发环境、架构和项目结构说明。将当前“使用说明”中的完整 AT 指令表、错误码列表、充电/驱动/温度/电机长篇语义移除，替换为：

```markdown
## 固件通信烟雾测试

UART1 使用 9600 8N1、无流控。发送 `AT+VERSION?\r\n`，当前固件应返回 `+VERSION:release-v3.3` 后跟 `OK`。完整接入流程、协议格式和现场操作分别见上方三份产品文档。
```

README 可以保留能力摘要，但不得再次维护完整命令或错误码表。

- [ ] **Step 3: 调整硬件相关表述的定位**

“先决条件”“主要引脚”“UART 引脚”继续作为本仓库开发和调试信息存在，但在其开头标注：这些内容不构成对外硬件产品规格，供电、电平、连接器、机械和环境要求以独立硬件规格书为准。

- [ ] **Step 4: 更新相关文件和清理旧链接**

将 README 底部相关文件列表更新为三份产品文档，并运行：

```powershell
rg -n "设备固件与上位机集成交接文档|已实现的 AT 指令|^ERROR:" -- README.md
```

期望：无输出。

- [ ] **Step 5: 提交 README 导航调整**

```powershell
git add -- README.md
git commit -m "docs: add product documentation navigation"
```

期望：提交只包含 README。

### Task 5: 验证三层文档体系与当前固件一致

**Files:**
- Verify: `README.md`
- Verify: `stem-hub模块集成与使用说明.md`
- Verify: `上位机AT命令文档.md`
- Verify: `stem-hub模块操作说明.md`
- Verify: `App/Inc/app_config.h`
- Verify: `App/Inc/app_at_protocol.h`
- Verify: `App/Src/app_at_protocol.c`
- Verify: `App/Src/app_at_task.c`
- Test: `tests/test_at_protocol.c`
- Test: `tests/test_*.py`

- [ ] **Step 1: 检查文件职责和旧入口清理**

```powershell
Test-Path -LiteralPath "设备固件与上位机集成交接文档.md"
rg -n "设备固件与上位机集成交接文档" --glob "*.md"
rg -n "RTOS|FreeRTOS|消息队列|事件组|任务栈|GPIO|ADC|App/Src/|Core/Src/" -- "stem-hub模块操作说明.md"
```

期望：`Test-Path` 返回 `False`；后两个命令无输出。

- [ ] **Step 2: 检查文档互链**

运行以下 PowerShell 检查器：

```powershell
$docs = @(
  'README.md',
  'stem-hub模块集成与使用说明.md',
  '上位机AT命令文档.md',
  'stem-hub模块操作说明.md'
)
$broken = @()
foreach ($doc in $docs) {
  $body = Get-Content -LiteralPath $doc -Raw
  $matches = [regex]::Matches($body, '\[[^\]]+\]\((?!https?://|#)([^)#]+\.md)(?:#[^)]+)?\)')
  foreach ($match in $matches) {
    $target = $match.Groups[1].Value -replace '^\./', ''
    $resolved = Join-Path (Split-Path -Parent (Resolve-Path -LiteralPath $doc)) $target
    if (-not (Test-Path -LiteralPath $resolved)) {
      $broken += "$doc -> $target"
    }
  }
}
if ($broken.Count -ne 0) { throw ($broken -join [Environment]::NewLine) }
"Markdown links OK"
```

期望：输出 `Markdown links OK`。

- [ ] **Step 3: 检查版本、状态和协议关键词覆盖**

```powershell
rg -n "release-v3\.3" README.md "stem-hub模块集成与使用说明.md" "上位机AT命令文档.md" "stem-hub模块操作说明.md"
rg -n "OFF|CHARGE|DRIVE|CHARGE_PHASE|SLEEP" -- "stem-hub模块集成与使用说明.md"
rg -n "UART2RX|UART3RX|ERROR:|AT\+VERSION|AT\+OUTPUT|AT\+MOTOR" -- "上位机AT命令文档.md"
```

期望：四份文档均标明 `release-v3.3`；主文档包含完整产品状态词；AT 文档包含异步事件、错误响应及关键查询命令。

- [ ] **Step 4: 运行协议和契约回归测试**

```powershell
gcc -std=c11 -Wall -Wextra -Werror -IApp/Inc tests/test_at_protocol.c App/Src/app_at_protocol.c -o "$env:TEMP\test_at_protocol.exe"
& "$env:TEMP\test_at_protocol.exe"
& 'C:\Users\44575\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -m pytest -q
```

期望：C 协议测试无输出且退出码为 0；pytest 全部通过（当前基线为 25 个测试）。

- [ ] **Step 5: 运行固件构建和差异检查**

```powershell
$env:PATH = 'C:\Users\44575\.vscode\extensions\stmicroelectronics.stm32cube-ide-core-1.4.0-win32-x64\resources\binaries\win32\x86_64;C:\Users\44575\.vscode\extensions\stmicroelectronics.stm32cube-ide-build-cmake-1.46.0-win32-x64\resources\cube-cmake\win32\x86_64;' + $env:PATH
cube-cmake.exe --build --preset Debug
git diff --check
git status --short
```

期望：Debug 构建成功或输出 `ninja: no work to do.`；`git diff --check` 无错误；状态中只保留用户原有的 `.settings` 改动，不出现未提交的本次文档文件。

- [ ] **Step 6: 汇总交付结果**

向用户报告三份文档的绝对路径、各自面向的读者、协议/pytest/构建/链接检查结果，以及未纳入本次范围的硬件规格。不得声称已经验证任何未实际运行的检查。
