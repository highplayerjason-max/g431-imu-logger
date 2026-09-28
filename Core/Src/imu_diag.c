#include <string.h>

#include "imu_diag.h"
#include "icm42688.h"
#include "main.h"
#ifdef IMU_DIAG_FLASH_READ_STRESS
#include "w25q64.h"
#endif

volatile uint32_t g_imu_diag_done = 0U;
volatile int32_t g_imu_diag_init_status = -99;
volatile uint32_t g_imu_diag_boot_who = 0U;
volatile uint32_t g_imu_diag_samples = 0U;
volatile uint32_t g_imu_diag_zero_total = 0U;
volatile uint32_t g_imu_diag_ff_total = 0U;
volatile uint32_t g_imu_diag_hal_fail = 0U;
volatile uint32_t g_imu_diag_max_zero_run = 0U;
volatile uint32_t g_imu_diag_event_count = 0U;
volatile uint32_t g_imu_diag_flash_read_fail = 0U;
ImuDiagEvent g_imu_diag_events[IMU_DIAG_EVENT_CAPACITY];

static uint8_t read_register(uint8_t reg, uint8_t *value)
{
  uint8_t tx[2] = {(uint8_t)(reg | 0x80U), 0U};
  uint8_t rx[2] = {0U, 0U};
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2U, 100U);
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);
  *value = rx[1];
  return (uint8_t)status;
}

static void capture_registers(ImuDiagEvent *event)
{
  event->who_status = read_register(ICM42688_WHO_AM_I_REG, &event->who);
  event->pwr_status = read_register(ICM42688_PWR_MGMT0, &event->pwr);
  event->accel_status = read_register(ICM42688_ACCEL_CONFIG0, &event->accel_cfg);
  event->gyro_status = read_register(ICM42688_GYRO_CONFIG0, &event->gyro_cfg);
  event->bank_status = read_register(ICM42688_REG_BANK_SEL, &event->bank);
}

static void capture_event(uint8_t kind, uint32_t sample_index, uint32_t time_us,
                          HAL_StatusTypeDef hal_status, const uint8_t rx[13],
                          const uint8_t last_good[12], uint32_t spi_error,
                          uint32_t run_length)
{
  if (g_imu_diag_event_count >= IMU_DIAG_EVENT_CAPACITY)
  {
    return;
  }

  ImuDiagEvent *event = &g_imu_diag_events[g_imu_diag_event_count++];
  event->sample_index = sample_index;
  event->time_us = time_us;
  event->run_length = run_length;
  event->spi_error = spi_error;
  event->kind = kind;
  event->hal_status = (uint8_t)hal_status;
  memcpy(event->rx, rx, 13U);
  memcpy(event->last_good, last_good, 12U);
  capture_registers(event);
}

void ImuDiag_Run(void)
{
  ICM42688_Handle imu = {
    .hspi = &hspi1,
    .cs_port = IMU_CS_GPIO_Port,
    .cs_pin = IMU_CS_Pin,
  };

  g_imu_diag_init_status = ICM42688_Init(&imu);
  g_imu_diag_boot_who = imu.who_am_i;
  if (g_imu_diag_init_status != ICM42688_OK)
  {
    g_imu_diag_done = 2U;
    return;
  }

#ifdef IMU_DIAG_FLASH_READ_STRESS
  if (W25Q64_Init() != W25Q64_OK)
  {
    g_imu_diag_done = 3U;
    return;
  }
  uint8_t flash_data[256];
#endif

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  const uint32_t cycles_per_sample = SystemCoreClock / 500U;
  uint32_t next_cycle = DWT->CYCCNT + cycles_per_sample;
  uint32_t zero_run = 0U;
  uint8_t last_good[12] = {0U};

  for (uint32_t index = 0U; index < IMU_DIAG_SAMPLE_COUNT; index++)
  {
    while ((int32_t)(DWT->CYCCNT - next_cycle) < 0)
    {
    }
    next_cycle += cycles_per_sample;
    uint32_t time_us = DWT->CYCCNT / (SystemCoreClock / 1000000U);

    uint8_t tx[13] = {(uint8_t)(ICM42688_ACCEL_DATA_X1 | 0x80U)};
    uint8_t rx[13];
    memset(rx, 0xA5, sizeof(rx));
    HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, 13U, 100U);
    HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);
    uint32_t spi_error = hspi1.ErrorCode;

    uint8_t all_zero = 1U;
    uint8_t all_ff = 1U;
    for (uint32_t j = 1U; j < 13U; j++)
    {
      if (rx[j] != 0U) all_zero = 0U;
      if (rx[j] != 0xFFU) all_ff = 0U;
    }

    if (status != HAL_OK)
    {
      g_imu_diag_hal_fail++;
      capture_event(3U, index, time_us, status, rx, last_good, spi_error, zero_run);
      zero_run = 0U;
    }
    else if (all_zero != 0U)
    {
      g_imu_diag_zero_total++;
      zero_run++;
      if (zero_run > g_imu_diag_max_zero_run)
      {
        g_imu_diag_max_zero_run = zero_run;
      }
      if (zero_run == 1U || (zero_run % 100U) == 0U)
      {
        capture_event(1U, index, time_us, status, rx, last_good, spi_error, zero_run);
      }
    }
    else
    {
      zero_run = 0U;
      if (all_ff != 0U)
      {
        g_imu_diag_ff_total++;
        capture_event(2U, index, time_us, status, rx, last_good, spi_error, 0U);
      }
      else
      {
        memcpy(last_good, &rx[1], sizeof(last_good));
      }
    }

    g_imu_diag_samples = index + 1U;

#ifdef IMU_DIAG_FLASH_READ_STRESS
    if ((index % 16U) == 15U)
    {
      uint32_t address = 0x001000U + ((index / 16U) % 312U) * 256U;
      if (W25Q64_Read(address, flash_data, sizeof(flash_data)) != W25Q64_OK)
      {
        g_imu_diag_flash_read_fail++;
      }
    }
#endif
  }

  g_imu_diag_done = 1U;
}
