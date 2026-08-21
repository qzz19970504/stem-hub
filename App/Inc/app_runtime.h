#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "adc.h"
#include "cmsis_os.h"
#include "main.h"
#include "usart.h"
#include "app_config.h"
#include "app_types.h"

/* 诊断计数器（用于排查 "AT 命令收不到响应" 类问题）。
 * 字段意义:
 *   rx_isr_count            - USART1 IRQ 总触发次数（在中断入口直接 ++）
 *   rx_byte_count           - App_RuntimePushUart1Byte 调用次数
 *   rx_overflow_count       - 环形缓冲满时丢字节的次数
 *   rx_error_count          - HAL_UART_ErrorCallback 总次数
 *   rx_ore_count            - 硬件 overrun 错误次数
 *   rx_ne_count             - 噪声错误次数
 *   rx_fe_count             - 帧错误次数
 *   rx_pe_count             - 奇偶校验错误次数
 *   line_too_long_count     - atTask 行缓冲溢出次数
 *   at_loop_count           - atTask 主循环 acquire 成功次数（任务还活着的证据）
 *   tx_call_count           - App_RuntimeSendText 调用次数
 *   tx_completed_count      - HAL_UART_Transmit 返回 HAL_OK 的次数
 *   tx_timeout_count        - HAL_UART_Transmit 返回 HAL_TIMEOUT 的次数
 *   tx_error_count          - HAL_UART_Transmit 返回 HAL_ERROR 的次数
 *   tx_busy_count           - HAL_UART_Transmit 返回 HAL_BUSY 的次数
 *   tx_state_pre            - 最近一次发送前 huart->gState 数值
 *   tx_state_post           - 最近一次发送后 huart->gState 数值
 *   tx_err_pre              - 最近一次发送前 huart->ErrorCode 位图
 *   tx_err_post             - 最近一次发送后 huart->ErrorCode 位图
 *   tx_last_status          - 最近一次 HAL_UART_Transmit 原始返回值
 *
 * tx_busy_count 与 HAL 状态机快照字段在排查"AT 响应异常"问题时用于区分
 * HAL_UART 状态机卡死（HAL_BUSY）与 Cortex-M 异常（HardFault）。在已知
 * "栈溢出导致 MLSPERR" 是本仓库历史故障根因后，这些字段仍然保留为只读
 * 观测，不参与任何自动恢复逻辑。 */
typedef struct
{
    uint32_t rx_isr_count;
    uint32_t rx_byte_count;
    uint32_t rx_overflow_count;
    uint32_t rx_error_count;
    uint32_t rx_ore_count;
    uint32_t rx_ne_count;
    uint32_t rx_fe_count;
    uint32_t rx_pe_count;
    uint32_t line_too_long_count;
    uint32_t at_loop_count;
    uint32_t tx_call_count;
    uint32_t tx_completed_count;
    uint32_t tx_timeout_count;
    uint32_t tx_error_count;
    uint32_t tx_busy_count;
    uint32_t tx_state_pre;
    uint32_t tx_state_post;
    uint32_t tx_err_pre;
    uint32_t tx_err_post;
    uint32_t tx_last_status;
    uint32_t sensor_loop_count;
    uint32_t sensor_publish_count;
    uint32_t sensor_last_publish_tick;
    uint32_t sensor_adc1_read_fail_count;
    uint32_t sensor_adc2_read_fail_count;
    uint32_t uart2_rx_byte_count;
    uint32_t uart2_rx_overflow_count;
    uint32_t uart3_rx_byte_count;
    uint32_t uart3_rx_overflow_count;
} AppRuntimeDiag;

typedef struct
{
    uint8_t uart1_rx_byte;
    volatile uint16_t uart1_head;
    volatile uint16_t uart1_tail;
    uint8_t uart1_ring[APP_UART1_RING_BUFFER_SIZE];
    uint8_t uart2_rx_byte;
    volatile uint16_t uart2_head;
    volatile uint16_t uart2_tail;
    uint8_t uart2_ring[APP_UART_BRIDGE_RING_BUFFER_SIZE];
    uint8_t uart3_rx_byte;
    volatile uint16_t uart3_head;
    volatile uint16_t uart3_tail;
    uint8_t uart3_ring[APP_UART_BRIDGE_RING_BUFFER_SIZE];
    osSemaphoreId_t uart1_rx_semaphore;
    osSemaphoreId_t bridge_rx_semaphore;
    osSemaphoreId_t sensor_ready_semaphore;
    osMutexId_t uart_tx_mutex;
    osMutexId_t bridge_mutex;
    osMutexId_t sensor_mutex;
    osMutexId_t adc2_mutex;
    osMutexId_t state_mutex;
    osMessageQueueId_t motor_queue;
    osMessageQueueId_t led_queue;
    osMessageQueueId_t output_queue;
} AppRuntime;

extern AppRuntime g_app_runtime;

/* 直接在 USART1 IRQ 入口里 ++ 的轻量级钩子，不依赖 HAL 层，
 * 用来区分 "UART 硬件根本没产生中断" 和 "中断产生了但 HAL 处理后没动作"。
 * 暴露符号，定义在 stm32f1xx_it.c 里。 */
extern volatile uint32_t g_app_diag_usart1_isr_count;

void App_RuntimeCreateObjects(void);
void App_RuntimeInit(void);
void App_RuntimeStartUart1Receive(void);
void App_RuntimeStartBridgeReceive(void);
HAL_StatusTypeDef App_RuntimeSendBytes(UART_HandleTypeDef *uart,
                                       const uint8_t *data,
                                       uint16_t length,
                                       uint32_t timeout);
void App_RuntimeSendText(UART_HandleTypeDef *uart, const char *text);
void App_RuntimeSendOk(void);
void App_RuntimeSendError(const char *reason);
bool App_RuntimePushUart1Byte(uint8_t byte);
bool App_RuntimePopUart1Byte(uint8_t *byte);
bool App_RuntimePopBridgeByte(uint8_t uart_index, uint8_t *byte);
void App_RuntimeFlushBridgeRx(uint8_t uart_index);
void App_RuntimeSelectBridgeTarget(AppBridgeTarget target);
void App_RuntimeClearBridgeTarget(void);
void App_RuntimeLockBridge(void);
void App_RuntimeUnlockBridge(void);
uint32_t App_RuntimeRawToMillivolts(uint16_t raw);
bool App_RuntimeReadChannel(ADC_HandleTypeDef *adc, uint32_t channel, uint16_t *raw_value);
bool App_RuntimeReadAdc2Channel(uint32_t channel, uint16_t *raw_value);
void App_RuntimeGetDiag(AppRuntimeDiag *out);
void App_RuntimeNoteLineTooLong(void);
void App_RuntimeNoteAtLoop(void);
void App_RuntimeNoteSensorLoop(void);
void App_RuntimeNoteSensorPublish(uint32_t tick);
void App_RuntimeNoteSensorAdc1ReadFail(void);
void App_RuntimeNoteSensorAdc2ReadFail(void);

/* 故障现场记录：在 HardFault/MemManage/BusFault/UsageFault 与
 * Error_Handler 入口里被调用，把 gState/ErrorCode/CFSR/HFSR/LR 写到一个
 * 不会被复位清掉的 .bss 段并通过 USART1 寄存器级 polling 输出一段 ASCII
 * 短帧，用于"固件已死"状态下也能从 UART 拉出最近一次故障原因。
 *
 * 输出格式：
 *   +FAIL:H=<hint32> <gState32> <errCode32> <CFSR32> <HFSR32> <LR32>\r\n
 *
 * hint 取值：
 *   0xE11E0001U = Error_Handler 通用入口
 *   0xE11E0002U = HAL_UART_Receive_IT 在 App_RuntimeStartUart1Receive 失败
 *   0xE11E0003U = RTOS 对象创建返回 NULL（fail-fast）
 *   0xE11E0004U = HardFault
 *   0xE11E0005U = MemManage
 *   0xE11E0006U = BusFault
 *   0xE11E0007U = UsageFault
 *
 * CFSR/HFSR 位定义见 Cortex-M3 Architecture Reference Manual；
 * 已知关联：本仓库历史"AT 命令后跑飞"复现为 0x00020000 (MLSPERR)
 * + 0x40000000 (FORCED)，即 atTask 栈溢出导致 MemManage 升级为 HardFault。
 * 修复见 commit `feat(rtos): expand atTask stack and heap (plan B)`。 */
void App_RecordFailureAndPrint(uint32_t hint, uint32_t lr_value);

#endif
