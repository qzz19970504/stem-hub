/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_core.h"
#include "usart.h"   /* UART 回环测试用：huart1 */

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
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for atTask */
osThreadId_t atTaskHandle;
const osThreadAttr_t atTask_attributes = {
  .name = "atTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for sensorTask */
osThreadId_t sensorTaskHandle;
const osThreadAttr_t sensorTask_attributes = {
  .name = "sensorTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for motorTask */
osThreadId_t motorTaskHandle;
const osThreadAttr_t motorTask_attributes = {
  .name = "motorTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for ledTask */
osThreadId_t ledTaskHandle;
const osThreadAttr_t ledTask_attributes = {
  .name = "ledTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for nmosTask */
osThreadId_t nmosTaskHandle;
const osThreadAttr_t nmosTask_attributes = {
  .name = "nmosTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

static void App_FailFastIfThreadCreateFailed(osThreadId_t thread_handle);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

static void App_FailFastIfThreadCreateFailed(osThreadId_t thread_handle)
{
  if (thread_handle == NULL)
  {
    Error_Handler();
  }
}

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* --- UART 回环测试：暂停应用初始化 ---
  App_CoreCreateObjects();
  App_CoreInit();
  --- */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
  App_FailFastIfThreadCreateFailed(defaultTaskHandle);

  /* USER CODE BEGIN RTOS_THREADS */
  /* --- UART 回环测试：只跑 defaultTask，其余应用任务全部停用 ---
  atTaskHandle = osThreadNew(App_AtTask, NULL, &atTask_attributes);
  App_FailFastIfThreadCreateFailed(atTaskHandle);
  sensorTaskHandle = osThreadNew(App_SensorTask, NULL, &sensorTask_attributes);
  App_FailFastIfThreadCreateFailed(sensorTaskHandle);
  motorTaskHandle = osThreadNew(App_MotorTask, NULL, &motorTask_attributes);
  App_FailFastIfThreadCreateFailed(motorTaskHandle);
  ledTaskHandle = osThreadNew(App_LedTask, NULL, &ledTask_attributes);
  App_FailFastIfThreadCreateFailed(ledTaskHandle);
  nmosTaskHandle = osThreadNew(App_NmosTask, NULL, &nmosTask_attributes);
  App_FailFastIfThreadCreateFailed(nmosTaskHandle);
  --- */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* --- 硬编码 UART1 回环测试 ---
   * 链路：电脑 USB <-> USB转485模块 <-> 板上485-TTL转换电路 <-> USART1(PA9/PA10)
   * - 每 1s 主动往外发 "hello\r\n"，便于确认 TX 方向（485->USB）通
   * - 阻塞收一字节立刻原样发回，确认 RX 方向（USB->485->TTL->UART1 RX）通
   */
  const uint8_t hello[] = "hello\r\n";
  uint8_t rx_byte = 0;
  uint32_t last_tick = 0;
  /* Infinite loop */
  for(;;)
  {
    /* 周期性主动发 hello，方便观察下行链路 */
    if (osKernelGetTickCount() - last_tick >= 1000)
    {
      last_tick = osKernelGetTickCount();
      HAL_UART_Transmit(&huart1, (uint8_t *)hello, sizeof(hello) - 1, 50);
    }
    /* 阻塞收一字节（10ms 超时），收到后立刻原样回传（回环） */
    if (HAL_UART_Receive(&huart1, &rx_byte, 1, 10) == HAL_OK)
    {
      HAL_UART_Transmit(&huart1, &rx_byte, 1, 50);
    }
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

