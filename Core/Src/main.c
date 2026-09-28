/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32G431CBU6 + ICM-42688-P offline IMU logger.
  *
  *          Hardware:
  *            SPI1  PB3/PB4/PB5 + PB6(CS)  -> ICM-42688-P
  *            SPI2  PB13/PB14/PB15 + PB12(CS) -> W25Q64
  *            USART1 PA9(TX)/PA10(RX) 115200 8N1 -> PC GUI
  ******************************************************************************
  */

#include <stdio.h>

#include "main.h"
#include "stm32g4xx_it.h"
#include "icm42688.h"
#include "uart_link.h"
#include "logger.h"
#ifdef SWD_EXPORT_ONLY
#include "w25q64.h"
#endif
#ifdef IMU_DIAG_ONLY
#include "imu_diag.h"
#endif

SPI_HandleTypeDef hspi1;

#ifdef UART1_TEST_ONLY
/* Debugger-visible result for the standalone USART1 test. */
volatile HAL_StatusTypeDef g_uart1_test_status = HAL_BUSY;
volatile uint32_t g_uart1_test_count = 0U;
#endif

#ifdef IMU_RESET_TIMING_TEST
volatile uint32_t g_ucpd_cr3_before;
volatile uint32_t g_ucpd_cr3_after;
volatile uint32_t g_pb6_after_gpio;
volatile uint32_t g_pb6_after_uart;
volatile uint32_t g_pa9_after_uart;
volatile uint32_t g_pa10_after_uart;
volatile int32_t g_imu_timing_bank_status;
volatile int32_t g_imu_timing_reset_status;
volatile int32_t g_imu_timing_post_bank_status;
volatile int32_t g_imu_timing_before_status[10];
volatile int32_t g_imu_timing_after_status[10];
volatile uint8_t g_imu_timing_before[10];
volatile uint8_t g_imu_timing_after[10];
#endif

#ifdef SWD_EXPORT_ONLY
/* Read-only SWD export status, visible in the debugger. */
volatile W25Q64_Status g_swd_export_status = W25Q64_ERR_SPI;
volatile uint32_t g_swd_export_jedec = 0U;
LogHeader g_swd_export_header;
#endif

/* ------------------------------------------------------------------------- */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc_init = {0};
  RCC_ClkInitTypeDef clk_init = {0};

  /* 170 MHz requires the internal regulator in Range 1 Boost mode. */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST) != HAL_OK)
  {
    Error_Handler();
  }

  /*
   * System clock: 170 MHz from HSI16 via PLL.
   *   PLL input = 16 MHz / 4 = 4 MHz, VCO = 340 MHz, SYSCLK = 170 MHz
   */
  osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc_init.HSIState       = RCC_HSI_ON;
  osc_init.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc_init.PLL.PLLState   = RCC_PLL_ON;
  osc_init.PLL.PLLSource  = RCC_PLLSOURCE_HSI;
  osc_init.PLL.PLLM       = RCC_PLLM_DIV4;
  osc_init.PLL.PLLN       = 85;
  osc_init.PLL.PLLP       = RCC_PLLP_DIV7;
  osc_init.PLL.PLLQ       = RCC_PLLQ_DIV2;
  osc_init.PLL.PLLR       = RCC_PLLR_DIV2;

  if (HAL_RCC_OscConfig(&osc_init) != HAL_OK)
  {
    Error_Handler();
  }

  clk_init.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                            RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk_init.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk_init.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  clk_init.APB1CLKDivider = RCC_HCLK_DIV1;
  clk_init.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Manual chip-select pins: IMU CS (PB6) and Flash CS (PB12), idle HIGH.
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio_init = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();

  gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull  = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;

  /* IMU CS: PB6, default HIGH */
  gpio_init.Pin = IMU_CS_Pin;
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_Init(IMU_CS_GPIO_Port, &gpio_init);

  /* W25Q64 CS: PB12, default HIGH */
  gpio_init.Pin = FLASH_CS_Pin;
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_Init(FLASH_CS_GPIO_Port, &gpio_init);

#if USER_BTN_ENABLED
  /* User button: input with pull-up, active low */
  gpio_init.Pin  = USER_BTN_Pin;
  gpio_init.Mode = GPIO_MODE_INPUT;
  gpio_init.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(USER_BTN_GPIO_Port, &gpio_init);
#endif
}

/**
  * @brief SPI1 for ICM-42688-P: Mode 0, 8-bit, MSB first, SW NSS.
  *        APB2/PCLK2 = 170 MHz, prescaler 256 -> ~664 kHz.
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance               = SPI1;
  hspi1.Init.Mode              = SPI_MODE_MASTER;
  hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;   /* SPI Mode 0 */
  hspi1.Init.NSS               = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial     = 7U;
  hspi1.Init.CRCLength         = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;

  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* ------------------------------------------------------------------------- */
int main(void)
{
  HAL_Init();

#ifndef IMU_RESET_TIMING_TEST
  /* Normal builds disable the UCPD dead-battery pull-downs before GPIO init. */
  HAL_PWREx_DisableUCPDDeadBattery();
#endif

  SystemClock_Config();
#if defined(IMU_RESET_TIMING_TEST)
  MX_GPIO_Init();
  g_pb6_after_gpio = (uint32_t)HAL_GPIO_ReadPin(IMU_CS_GPIO_Port, IMU_CS_Pin);
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  g_pb6_after_uart = (uint32_t)HAL_GPIO_ReadPin(IMU_CS_GPIO_Port, IMU_CS_Pin);
  g_pa9_after_uart = (uint32_t)HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9);
  g_pa10_after_uart = (uint32_t)HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10);

  printf("\r\nUCPD diagnostic:\r\n");
  printf("PWR_CR3 before = 0x%08lX, after = 0x%08lX\r\n",
         (unsigned long)g_ucpd_cr3_before, (unsigned long)g_ucpd_cr3_after);
  printf("UCPD1_DBDIS = %lu\r\n",
         (unsigned long)((g_ucpd_cr3_after & PWR_CR3_UCPD_DBDIS) != 0U));
  printf("PB6 before GPIO init: not sampled (reset mode is analog)\r\n");
  printf("PB6 after GPIO init = %lu, after USART1 init = %lu\r\n",
         (unsigned long)g_pb6_after_gpio, (unsigned long)g_pb6_after_uart);
  printf("PA9 after USART1 init = %lu, PA10 = %lu\r\n",
         (unsigned long)g_pa9_after_uart, (unsigned long)g_pa10_after_uart);

  ICM42688_Handle imu = {
    .hspi = &hspi1,
    .cs_port = IMU_CS_GPIO_Port,
    .cs_pin = IMU_CS_Pin,
  };
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);
  HAL_Delay(20U);
  g_imu_timing_bank_status = ICM42688_WriteReg(&imu, ICM42688_REG_BANK_SEL, 0x00U);
  HAL_Delay(2U);
  printf("\r\nIMU timing test: bank0 write status %ld\r\n", (long)g_imu_timing_bank_status);
  for (uint32_t i = 0U; i < 10U; i++)
  {
    uint8_t who = 0xFFU;
    g_imu_timing_before_status[i] = ICM42688_ReadReg(&imu, ICM42688_WHO_AM_I_REG, &who);
    g_imu_timing_before[i] = who;
    printf("before reset WHO[%lu] = 0x%02X status %ld\r\n",
           (unsigned long)i, (unsigned)who, (long)g_imu_timing_before_status[i]);
  }

  g_imu_timing_reset_status = ICM42688_WriteReg(&imu, ICM42688_DEVICE_CONFIG, 0x01U);
  HAL_Delay(5U);
  g_imu_timing_post_bank_status = ICM42688_WriteReg(&imu, ICM42688_REG_BANK_SEL, 0x00U);
  HAL_Delay(2U);
  printf("software reset write status %ld, post-reset bank0 write status %ld\r\n",
         (long)g_imu_timing_reset_status, (long)g_imu_timing_post_bank_status);
  for (uint32_t i = 0U; i < 10U; i++)
  {
    uint8_t who = 0xFFU;
    g_imu_timing_after_status[i] = ICM42688_ReadReg(&imu, ICM42688_WHO_AM_I_REG, &who);
    g_imu_timing_after[i] = who;
    printf("after reset WHO[%lu] = 0x%02X status %ld\r\n",
           (unsigned long)i, (unsigned)who, (long)g_imu_timing_after_status[i]);
  }
  while (1)
  {
    HAL_Delay(1000U);
  }
#elif defined(IMU_DIAG_ONLY)
  MX_GPIO_Init();
  MX_SPI1_Init();
  ImuDiag_Run();
#elif defined(SWD_EXPORT_ONLY)
  MX_GPIO_Init();
  g_swd_export_status = W25Q64_Init();
  if (g_swd_export_status == W25Q64_OK)
  {
    uint8_t mf = 0U, mt = 0U, cap = 0U;
    g_swd_export_status = W25Q64_ReadJEDECID(&mf, &mt, &cap);
    g_swd_export_jedec = ((uint32_t)mf << 16U) | ((uint32_t)mt << 8U) | cap;
    if (g_swd_export_status == W25Q64_OK &&
        (mf != W25Q64_JEDEC_MANUFACTURER ||
         mt != W25Q64_JEDEC_MEMORY_TYPE ||
         cap != W25Q64_JEDEC_CAPACITY))
    {
      g_swd_export_status = W25Q64_ERR_VERIFY;
    }
    if (g_swd_export_status == W25Q64_OK)
    {
      g_swd_export_status = W25Q64_Read(LOG_META_ADDR,
                                         (uint8_t *)&g_swd_export_header,
                                         sizeof(g_swd_export_header));
    }
  }
  while (1)
  {
    HAL_Delay(1000U);
  }
#elif defined(UART1_TEST_ONLY)
  MX_USART1_UART_Init();

  static const uint8_t test_message[] = "UART1 TEST\r\n";
  while (1)
  {
    g_uart1_test_status = HAL_UART_Transmit(&huart1, (uint8_t *)test_message,
                                             sizeof(test_message) - 1U, 100U);
    if (g_uart1_test_status == HAL_OK)
    {
      g_uart1_test_count++;
    }
    HAL_Delay(1000U);
  }
#else
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();

  printf("\r\n=== G431 offline IMU logger ===\r\n");

  (void)Logger_Init();

  while (1)
  {
    Logger_Process();
  }
#endif
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  Error_Handler();
}
#endif /* USE_FULL_ASSERT */
