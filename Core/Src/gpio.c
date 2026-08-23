/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, MOTOR_BYPASS_Pin|CHARGE_BYPASS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  /* EN_UVLO (PB3) 默认拉高：LM51770 是低电平使能，高电平为关断。*/
  HAL_GPIO_WritePin(GPIOB, EN_IN1_Pin|PH_IN2_Pin|nSLEEP_Pin|EN_UVLO_Pin
                          |NMOS2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(EN_UVLO_GPIO_Port, EN_UVLO_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  /* PA8 (MP4317) 默认拉高：MP4317 是低电平使能，高电平为关断。*/
  HAL_GPIO_WritePin(GPIOA, NMOS1_Pin|LED3_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MP4317_GPIO_Port, MP4317_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : LED1_Pin */
  GPIO_InitStruct.Pin = LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : MOTOR_BYPASS_Pin CHARGE_BYPASS_Pin */
  GPIO_InitStruct.Pin = MOTOR_BYPASS_Pin|CHARGE_BYPASS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : EN_IN1_Pin PH_IN2_Pin nSLEEP_Pin EN_UVLO_Pin
                           NMOS2_Pin */
  GPIO_InitStruct.Pin = EN_IN1_Pin|PH_IN2_Pin|nSLEEP_Pin|EN_UVLO_Pin
                          |NMOS2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : nFAULT_Pin */
  GPIO_InitStruct.Pin = nFAULT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(nFAULT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : NMOS1_Pin LED3_Pin MP4317_Pin */
  GPIO_InitStruct.Pin = NMOS1_Pin|LED3_Pin|MP4317_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : nFLT_Pin */
  GPIO_InitStruct.Pin = nFLT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(nFLT_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
