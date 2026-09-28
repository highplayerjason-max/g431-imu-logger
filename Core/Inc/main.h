/**
  ******************************************************************************
  * @file    main.h
  * @brief   Main header: board definitions shared by app modules.
  ******************************************************************************
  */

#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"

/* SPI1 / ICM-42688-P wiring */
#define IMU_CS_Pin          GPIO_PIN_6
#define IMU_CS_GPIO_Port    GPIOB

#define IMU_INT1_Pin        GPIO_PIN_7
#define IMU_INT1_GPIO_Port  GPIOB

/* SPI2 / W25Q64 wiring */
#define FLASH_CS_Pin        GPIO_PIN_12
#define FLASH_CS_GPIO_Port  GPIOB

/* User button for the offline logger.
   Active low, internal pull-up, debounced in software.
   Change these two lines to match your board. */
#define USER_BTN_ENABLED    1
#define USER_BTN_Pin        GPIO_PIN_10
#define USER_BTN_GPIO_Port  GPIOB

/* SPI handles */
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim6;

void Error_Handler(void);
void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
