/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define MOTOR_BYPASS_Pin GPIO_PIN_13
#define MOTOR_BYPASS_GPIO_Port GPIOC
#define CHARGE_BYPASS_Pin GPIO_PIN_14
#define CHARGE_BYPASS_GPIO_Port GPIOC
#define LED1_Pin GPIO_PIN_15
#define LED1_GPIO_Port GPIOC
#define EN_IN1_Pin GPIO_PIN_12
#define EN_IN1_GPIO_Port GPIOB
#define PH_IN2_Pin GPIO_PIN_13
#define PH_IN2_GPIO_Port GPIOB
#define nSLEEP_Pin GPIO_PIN_14
#define nSLEEP_GPIO_Port GPIOB
#define nFAULT_Pin GPIO_PIN_15
#define nFAULT_GPIO_Port GPIOB
#define MP4317_Pin GPIO_PIN_8
#define MP4317_GPIO_Port GPIOA
#define NMOS1_Pin GPIO_PIN_12
#define NMOS1_GPIO_Port GPIOA
#define LED3_Pin GPIO_PIN_15
#define LED3_GPIO_Port GPIOA
#define nFLT_Pin GPIO_PIN_11
#define nFLT_GPIO_Port GPIOA
#define EN_UVLO_Pin GPIO_PIN_3
#define EN_UVLO_GPIO_Port GPIOB
#define NMOS2_Pin GPIO_PIN_4
#define NMOS2_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
