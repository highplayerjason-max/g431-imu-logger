/**
  ******************************************************************************
  * @file    uart_link.c
  * @brief   USART1 telemetry link + printf retarget.
  *
  *          Wiring:
  *            MCU PA9  (USART1_TX) -> ST-Link RXD
  *            MCU PA10 (USART1_RX) -> ST-Link TXD
  *            GND common
  *          PC opens the ST-Link Virtual COM port (115200 8N1).
  ******************************************************************************
  */

#include "uart_link.h"
#include "main.h"

UART_HandleTypeDef huart1;

void MX_USART1_UART_Init(void)
{
  huart1.Instance          = USART1;
  huart1.Init.BaudRate     = 115200;
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;

  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* printf() -> USART1 */
int __io_putchar(int ch)
{
  uint8_t byte = (uint8_t)ch;
  HAL_UART_Transmit(&huart1, &byte, 1U, 10U);
  return ch;
}
