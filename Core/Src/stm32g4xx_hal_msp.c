/**
  ******************************************************************************
  * @file    stm32g4xx_hal_msp.c
  * @brief   Low level (MSP) initialisation for the HAL peripherals used here.
  ******************************************************************************
  */

#include "main.h"

/**
  * @brief Global MSP init. Nothing special is required for this board.
  */
void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
#ifdef IMU_RESET_TIMING_TEST
  extern volatile uint32_t g_ucpd_cr3_before;
  extern volatile uint32_t g_ucpd_cr3_after;
  g_ucpd_cr3_before = PWR->CR3;
#ifndef UCPD_TEST_KEEP_ENABLED
  HAL_PWREx_DisableUCPDDeadBattery();
#endif
  g_ucpd_cr3_after = PWR->CR3;
#endif
}

/**
  * @brief SPI MSP initialisation: SPI1 clock + PB3/PB4/PB5 alternate function.
  *        PB6 (CS) is intentionally NOT configured here; it is a manual GPIO
  *        output in MX_GPIO_Init().
  */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
  GPIO_InitTypeDef gpio_init = {0};

  if (hspi->Instance == SPI1)
  {
    /* Peripheral clock */
    __HAL_RCC_SPI1_CLK_ENABLE();

    /* GPIO clock */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB3  -> SPI1_SCK  (AF5)
       PB4  -> SPI1_MISO (AF5)
       PB5  -> SPI1_MOSI (AF5) */
    gpio_init.Pin       = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = GPIO_NOPULL;
    gpio_init.Speed     = GPIO_SPEED_FREQ_LOW;  /* ~1 MHz SPI -> low speed is enough */
    gpio_init.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOB, &gpio_init);
  }
  else if (hspi->Instance == SPI2)
  {
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB13 -> SPI2_SCK  (AF5)
       PB14 -> SPI2_MISO (AF5)
       PB15 -> SPI2_MOSI (AF5) */
    gpio_init.Pin       = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = GPIO_NOPULL;
    gpio_init.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &gpio_init);
  }
}

/**
  * @brief SPI MSP de-initialisation.
  */
void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1)
  {
    __HAL_RCC_SPI1_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);
  }
  else if (hspi->Instance == SPI2)
  {
    __HAL_RCC_SPI2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
  }
}

/**
  * @brief USART1 MSP init: clock + PA9(TX)/PA10(RX) alternate function.
  */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef gpio_init = {0};

  if (huart->Instance == USART1)
  {
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA9  -> USART1_TX (AF7)
       PA10 -> USART1_RX (AF7) */
    gpio_init.Pin       = GPIO_PIN_9 | GPIO_PIN_10;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = GPIO_PULLUP;
    gpio_init.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio_init);
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    __HAL_RCC_USART1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);
  }
}

/**
  * @brief TIM6 MSP init: 500 Hz sampling time base.
  */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    __HAL_RCC_TIM6_CLK_ENABLE();

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
  }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    __HAL_RCC_TIM6_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
  }
}
