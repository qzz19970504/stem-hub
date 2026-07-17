/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "adc.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_core.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#include <stdint.h>
#include "usart.h"
#include "stm32f1xx_hal.h"

/* 故障现场记录 + 最后机会 UART 输出。
 *
 * 在 HardFault/MemManage/BusFault/UsageFault 入口以及 Error_Handler /
 * RTOS 对象创建失败等"固件即将进入 while(1) 死循环"的关键点被调用。
 * 不依赖任何 RTOS/HAL callback，不读任何 task 栈——只读全局 huart1 句柄
 * 与 Cortex-M3 系统控制块寄存器，并直接轮询 USART1->SR/DR 寄存器，
 * 在 __disable_irq 之前把一行 ASCII 短帧写到 UART1 物理线上。
 *
 * 字段（g_fail_record[8]）：
 *   [0] = magic (0xFA115E11)
 *   [1] = huart1.gState (触发时刻)
 *   [2] = huart1.ErrorCode
 *   [3] = SCB->CFSR (Configurable Fault Status)
 *   [4] = SCB->HFSR (HardFault Status)
 *   [5] = LR（链接寄存器，反映调用现场）
 *   [6] = hint（来自调用者的来源估计，0 = 未知）
 *   [7] = magic XOR（用于校验）
 *
 * UART1 输出格式：`+FAIL:H=<hint32> <gState32> <errCode32> <CFSR32>
 *                  <HFSR32> <LR32>\r\n`
 *
 * 已知关联：本仓库"AT 命令后跑飞"复现中 CFSR=0x00020000 (MLSPERR) +
 * HFSR=0x40000000 (FORCED)，即 atTask 栈溢出导致 MemManage 升级为
 * HardFault。修复见 commit `feat(rtos): expand atTask stack and heap
 * (plan B)` 与 docs/at-rx-stall-debug-report.md §6。 */
static volatile uint32_t g_fail_record[8] = {0};

void App_RecordFailureAndPrint(uint32_t hint, uint32_t lr_value)
{
    if (g_fail_record[0] == 0xFA115E11U)
    {
        /* 已经被记录过，避免重复覆盖现场。 */
        return;
    }

    g_fail_record[0] = 0xFA115E11U;
    g_fail_record[1] = (uint32_t)huart1.gState;
    g_fail_record[2] = (uint32_t)huart1.ErrorCode;
    g_fail_record[3] = (uint32_t)SCB->CFSR;
    g_fail_record[4] = (uint32_t)SCB->HFSR;
    g_fail_record[5] = lr_value;
    g_fail_record[6] = hint;
    g_fail_record[7] = 0xFA115E11U ^ g_fail_record[1] ^ g_fail_record[2] ^
                       g_fail_record[3] ^ g_fail_record[4] ^ g_fail_record[5] ^
                       g_fail_record[6];

    /* 在 __disable_irq 之前用 polling 输出一段 ASCII 短帧；这样即使
     * 接下来进入 while(1)，PC 端也能在串口监视器里读到这次失败原因。 */
    const char *prefix = "+FAIL:H=";
    for (const char *p = prefix; *p != '\0'; ++p)
    {
        while ((USART1->SR & USART_SR_TXE) == 0U)
        {
        }
        USART1->DR = (uint16_t)(*p & 0x01FFU);
    }

    static const char hex[] = "0123456789ABCDEF";
    uint32_t values[6] = {
        hint,
        g_fail_record[1],
        g_fail_record[2],
        g_fail_record[3],
        g_fail_record[4],
        g_fail_record[5]
    };
    for (uint32_t i = 0U; i < 6U; ++i)
    {
        for (int32_t s = 28; s >= 0; s -= 4)
        {
            uint8_t nibble = (uint8_t)((values[i] >> (uint32_t)s) & 0x0FU);
            while ((USART1->SR & USART_SR_TXE) == 0U)
            {
            }
            USART1->DR = (uint16_t)(hex[nibble] & 0x01FFU);
        }
        if (i + 1U < 6U)
        {
            while ((USART1->SR & USART_SR_TXE) == 0U)
            {
            }
            USART1->DR = (uint16_t)(' ' & 0x01FFU);
        }
    }

    while ((USART1->SR & USART_SR_TXE) == 0U)
    {
    }
    USART1->DR = (uint16_t)('\r' & 0x01FFU);
    while ((USART1->SR & USART_SR_TXE) == 0U)
    {
    }
    USART1->DR = (uint16_t)('\n' & 0x01FFU);
}

static void App_PreInitMotorSafeState(void)
{
  GPIO_InitTypeDef safe_gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  HAL_GPIO_WritePin(GPIOB, nSLEEP_Pin|EN_IN1_Pin|PH_IN2_Pin, GPIO_PIN_RESET);

  safe_gpio.Pin = nSLEEP_Pin|EN_IN1_Pin|PH_IN2_Pin;
  safe_gpio.Mode = GPIO_MODE_OUTPUT_PP;
  safe_gpio.Pull = GPIO_NOPULL;
  safe_gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &safe_gpio);
  HAL_GPIO_WritePin(GPIOB, nSLEEP_Pin|EN_IN1_Pin|PH_IN2_Pin, GPIO_PIN_RESET);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  App_PreInitMotorSafeState();

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();

  MX_USART1_UART_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV8;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  App_RecordFailureAndPrint(0xE11E0001U, (uint32_t)__builtin_return_address(0));
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
