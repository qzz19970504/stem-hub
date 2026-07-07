# Prompt：PH/EN 模式 H 桥电机驱动芯片（FreeRTOS 任务模块版）

## 场景
- 5 个引脚：nSLEEP、EN、PH、ADC（电流反馈），无 PWM
- 作为 FreeRTOS 中的一个**任务模块**使用
- 不需要看门狗、不需要 HardFault 处理（系统级的事交给系统级）

---

## 1. 真值表（PH/EN 模式）

| nSLEEP | EN | PH | OUT1 | OUT2 | 说明 |
|--------|----|----|------|------|------|
| 0 | X | X | Hi-Z | Hi-Z | **睡眠**（电机完全失电）|
| 1 | 0 | X | L | L | **制动** |
| 1 | 1 | 0 | L | H | **后退** |
| 1 | 1 | 1 | H | L | **前进** |

nSLEEP 是总开关，=0 时 EN/PH 完全无效。

---

## 2. 操作 API（任务里就这么调）

```
motor_sleep();          // nSLEEP=0
motor_wake();           // nSLEEP=1
motor_brake();          // EN=0
motor_forward();        // PH=1
motor_reverse();        // PH=0
motor_run(dir, on);     // 一键：方向 + 通断
motor_read_current();   // ADC 读电流
```

**顺序规则**：
- 启动：sleep → wake → 方向 → EN=1
- 制动：EN=0
- 断电：nSLEEP=0
- 换向：先 EN=0 制动，等几 ms，再翻 PH，再 EN=1

---

## 3. 上电初始化（关键，main 启动调度器之前）

**核心：nSLEEP 必须在 FreeRTOS 调度器启动前就被锁在低电平。**

```c
int main(void) {
    // 1. main 第一行：先把 nSLEEP 拉低（锁死睡眠状态）
    __HAL_RCC_GPIOX_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = NSLEEP_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;          // 芯片已有内部下拉就不要叠
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(NSLEEP_PORT, &gpio);
    HAL_GPIO_WritePin(NSLEEP_PORT, NSLEEP_PIN, GPIO_PIN_RESET);

    // 2. 后面才是常规初始化
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();     // EN、PH 也配好，默认 EN=0（制动）
    MX_ADC1_Init();     // ADC 配好

    // 3. 创建 FreeRTOS 任务
    xTaskCreate(motor_task, "motor", 256, NULL, 3, NULL);
    vTaskStartScheduler();   // 启动后任务才能调 motor_wake()
}

void motor_task(void *arg) {
    while (1) {
        // 业务逻辑：通过队列/信号量接收控制命令
        // motor_run(FORWARD, ON);
        // motor_read_current();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

**为什么这样写**：
- 调度器没启动前，所有任务代码都跑不到 → nSLEEP 必须靠 main 硬初始化锁住
- 内部下拉 + MCU 第一行拉低 = 上电瞬间双重保险
- 任务里只管业务逻辑，不管初始化时序

---

## 4. 电流反馈（ADC）

第 5 个脚是 IPROPI 电流输出：
- 芯片把电机电流按比例映射成电压，从 IPROPI 引脚输出
- ADC 读这个电压 → 反推电流
- 用途：堵转检测、过流保护、负载判断

读取时机：必须 nSLEEP=1 且 EN=1 时才有意义。

---

## 5. 易踩的坑

1. **nSLEEP 拉低 = 进睡眠**，不是启动
2. **EN=0 是制动不是断电**
3. **EN=0 时改 PH 无意义**
4. **上电瞬间 GPIO 是浮空的**，EN/PH 不可控
5. **内部下拉阻值大（500kΩ~1MΩ）**，抗干扰弱，长走线或电源毛刺可能灌穿
6. **PH 翻转瞬间有直通风险**，换向前先 EN=0 + 死区延时
7. **FreeRTOS 任务里不能做 nSLEEP 初始化**——任务跑起来前那段时间没人管

---

## 6. Checklist

- [ ] 查数据手册：**nSLEEP 内部下拉规格**
- [ ] main 第一行：nSLEEP 拉低
- [ ] EN/PH 在调度器启动前配到默认安全态（EN=0）
- [ ] 电流 ADC 采样在 EN=1 时才有意义
- [ ] 换向：先 EN=0，加死区，再翻 PH

---

## 使用方式
作为 system prompt / context 喂给 AI，回答 PH/EN 模式电机驱动问题时严格按这套来——核心是 **FreeRTOS 上下文的 fail-safe 初始化** + **简化的 API 调用**。