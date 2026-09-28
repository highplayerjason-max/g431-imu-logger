/**
  ******************************************************************************
  * @file    stm32g4xx_hal_conf.h
  * @brief   HAL configuration for the STM32G431CBU6 VSCode/CMake project.
  *          Only the modules used by this bring-up build are enabled.
  ******************************************************************************
  */

#ifndef STM32G4xx_HAL_CONF_H
#define STM32G4xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------- Module selection ---------------------------- */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* ----------------------- Register callbacks selection -------------------- */
#define USE_HAL_SPI_REGISTER_CALLBACKS       0U

/* ----------------------- Oscillator values adaptation -------------------- */
#if !defined(HSE_VALUE)
#define HSE_VALUE                (8000000UL)  /* external crystal if fitted   */
#endif

#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT      (100UL)
#endif

#if !defined(HSI_VALUE)
#define HSI_VALUE                (16000000UL) /* STM32G4 HSI16                */
#endif

#if !defined(HSI48_VALUE)
#define HSI48_VALUE              (48000000UL)
#endif

#if !defined(LSI_VALUE)
#define LSI_VALUE                (32000UL)
#endif

#if !defined(LSE_VALUE)
#define LSE_VALUE                (32768UL)
#endif

#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT      (5000UL)
#endif

#if !defined(EXTERNAL_CLOCK_VALUE)
#define EXTERNAL_CLOCK_VALUE     (48000UL)
#endif

/* ---------------------------- System configuration ----------------------- */
#define VDD_VALUE                 (3300UL)
#define TICK_INT_PRIORITY         (0x0FUL)
#define USE_RTOS                  0U
#define PREFETCH_ENABLE           0U
#define INSTRUCTION_CACHE_ENABLE  1U
#define DATA_CACHE_ENABLE         1U

/* ---------------------------- Assert selection --------------------------- */
/* #define USE_FULL_ASSERT        1U */

#ifdef USE_FULL_ASSERT
/**
  * @brief  The assert_param macro is used for function's parameters check.
  */
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif /* USE_FULL_ASSERT */

/* --------------------- SPI peripheral configuration ---------------------- */
#define USE_SPI_CRC               0U

/* ------------------------------- Includes -------------------------------- */
#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32g4xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32g4xx_hal_gpio.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32g4xx_hal_dma.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32g4xx_hal_cortex.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32g4xx_hal_flash.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32g4xx_hal_pwr.h"
#endif

#ifdef HAL_SPI_MODULE_ENABLED
#include "stm32g4xx_hal_spi.h"
#endif

#ifdef HAL_TIM_MODULE_ENABLED
#include "stm32g4xx_hal_tim.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
#include "stm32g4xx_hal_uart.h"
#endif

#ifdef HAL_MODULE_ENABLED
#include "stm32g4xx_hal.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32G4xx_HAL_CONF_H */
