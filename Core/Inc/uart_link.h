/**
  ******************************************************************************
  * @file    uart_link.h
  * @brief   USART1 debug/telemetry output (PA9=TX, PA10=RX, 115200 8N1).
  ******************************************************************************
  */

#ifndef UART_LINK_H
#define UART_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

void MX_USART1_UART_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_LINK_H */
