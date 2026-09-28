/**
  ******************************************************************************
  * @file    icm42688.c
  * @brief   Minimal polling SPI driver for TDK InvenSense ICM-42688-P.
  *
  * SPI protocol notes:
  *  - read:  first byte = 0x80 | reg, following bytes return data.
  *  - write: first byte = reg (bit7 = 0), second byte = data.
  *  - CS is asserted low before the first byte and released after the last.
  ******************************************************************************
  */

#include <string.h>

#include "icm42688.h"

#define ICM42688_SPI_TIMEOUT  100U
#define ICM42688_BOOT_DELAY_MS    20U
#define ICM42688_BANK_DELAY_MS     2U
#define ICM42688_RESET_DELAY_MS    5U
#define ICM42688_STARTUP_DELAY_MS 50U

/* ------------------------------------------------------------------------- */
static inline int16_t combine_u8(int16_t high, int16_t low)
{
  /* CMSIS/C99 guaranteed path: assemble unsigned 16-bit first,
     then convert to int16_t (implementation-defined but universally
     two's complement on Cortex-M GCC/Clang). */
  return (int16_t)(((uint16_t)high << 8U) | (uint16_t)low);
}

/**
  * @brief Run one full-duplex SPI transaction with CS controlled around it.
  */
static uint8_t spi_transfer(ICM42688_Handle *dev,
                            const uint8_t *tx, uint8_t *rx, uint16_t len)
{
  if ((dev == 0) || (dev->hspi == 0) || (dev->cs_port == 0) || (len == 0U))
  {
    return 0U;
  }

  HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);

  /* CS setup time before the first SCLK edge. The ICM needs >=39 ns per the
     datasheet, but this board (module pull-ups + wiring) needs more margin:
     without this delay the HAL path reads WHO_AM_I = 0x00 while a manual
     bit-bang read returns 0x47. */
  for (volatile uint32_t cs_setup = 0U; cs_setup < 64U; cs_setup++)
  {
    __NOP();
  }

  HAL_StatusTypeDef hal_status =
      HAL_SPI_TransmitReceive(dev->hspi, (uint8_t *)tx, rx, len, ICM42688_SPI_TIMEOUT);
  HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);

  return (hal_status == HAL_OK) ? 1U : 0U;
}

/* ------------------------------------------------------------------------- */
ICM42688_Status ICM42688_ReadReg(ICM42688_Handle *dev, uint8_t reg, uint8_t *value)
{
  uint8_t tx[2];
  uint8_t rx[2];

  if ((dev == 0) || (value == 0))
  {
    return ICM42688_ERR_READ;
  }

  tx[0] = (uint8_t)(0x80U | reg);
  tx[1] = 0x00U;

  if (spi_transfer(dev, tx, rx, 2U) == 0U)
  {
    return ICM42688_ERR_READ;
  }

  *value = rx[1];   /* first received byte echoes the address byte */
  return ICM42688_OK;
}

ICM42688_Status ICM42688_ReadRegs(ICM42688_Handle *dev, uint8_t reg,
                                   uint8_t *data, uint16_t len)
{
  uint8_t tx[1U + ICM42688_RAW_DATA_LEN];
  uint8_t rx[1U + ICM42688_RAW_DATA_LEN];

  if ((dev == 0) || (data == 0) || (len == 0U) || (len > ICM42688_RAW_DATA_LEN))
  {
    return ICM42688_ERR_READ;
  }

  tx[0] = (uint8_t)(0x80U | reg);
  memset(&tx[1], 0x00U, len);

  if (spi_transfer(dev, tx, rx, (uint16_t)(len + 1U)) == 0U)
  {
    return ICM42688_ERR_READ;
  }

  memcpy(data, &rx[1], len);
  return ICM42688_OK;
}

ICM42688_Status ICM42688_WriteReg(ICM42688_Handle *dev, uint8_t reg, uint8_t value)
{
  uint8_t tx[2];
  uint8_t rx[2];

  if (dev == 0)
  {
    return ICM42688_ERR_WRITE;
  }

  tx[0] = (uint8_t)(reg & 0x7FU);
  tx[1] = value;

  if (spi_transfer(dev, tx, rx, 2U) == 0U)
  {
    return ICM42688_ERR_WRITE;
  }

  return ICM42688_OK;
}

/* ------------------------------------------------------------------------- */
ICM42688_Status ICM42688_Init(ICM42688_Handle *dev)
{
  ICM42688_Status status;
  uint8_t config0;

  if ((dev == 0) || (dev->hspi == 0) || (dev->cs_port == 0))
  {
    return ICM42688_ERR_READ;
  }

  dev->who_am_i   = 0xFFU;
  dev->init_status = ICM42688_OK;

  /* An MCU reset does not necessarily reset the separately powered IMU. */
  HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
  HAL_Delay(ICM42688_BOOT_DELAY_MS);

  /* REG_BANK_SEL is accessible in each bank; force Bank 0 before DEVICE_CONFIG. */
  status = ICM42688_WriteReg(dev, ICM42688_REG_BANK_SEL, 0x00U);
  if (status != ICM42688_OK)
  {
    dev->init_status = status;
    return status;
  }
  HAL_Delay(ICM42688_BANK_DELAY_MS);

  /* Software reset may change the active bank. Give it time to complete. */
  status = ICM42688_WriteReg(dev, ICM42688_DEVICE_CONFIG, 0x01U);
  if (status != ICM42688_OK)
  {
    dev->init_status = status;
    return status;
  }
  HAL_Delay(ICM42688_RESET_DELAY_MS);

  status = ICM42688_WriteReg(dev, ICM42688_REG_BANK_SEL, 0x00U);
  if (status != ICM42688_OK)
  {
    dev->init_status = status;
    return status;
  }
  HAL_Delay(ICM42688_BANK_DELAY_MS);

  /* First hardware check: WHO_AM_I must be 0x47. */
  status = ICM42688_ReadReg(dev, ICM42688_WHO_AM_I_REG, &dev->who_am_i);
  if (status != ICM42688_OK)
  {
    dev->init_status = status;
    return status;
  }

  if (dev->who_am_i != ICM42688_WHO_AM_I_EXPECTED)
  {
    dev->init_status = ICM42688_ERR_ID;
    return ICM42688_ERR_ID;
  }

  /*
   * Explicitly set UI output data rate to 1 kHz while keeping the default
   * full scale ranges:
   *   gyro  : +/-2000 dps  (GYRO_FS_SEL = 0)
   *   accel : +/-16 g      (ACCEL_FS_SEL = 0)
   * Config0 layout: [7:5] FS_SEL, [4] reserved, [3:0] ODR
   */
  config0 = (uint8_t)(((ICM42688_GYRO_FS_2000DPS & 0x07U) << 5U) | ICM42688_ODR_1KHZ);
  status = ICM42688_WriteReg(dev, ICM42688_GYRO_CONFIG0, config0);
  if (status != ICM42688_OK)
  {
    dev->init_status = status;
    return status;
  }

  config0 = (uint8_t)(((ICM42688_ACCEL_FS_16G & 0x07U) << 5U) | ICM42688_ODR_1KHZ);
  status = ICM42688_WriteReg(dev, ICM42688_ACCEL_CONFIG0, config0);
  if (status != ICM42688_OK)
  {
    dev->init_status = status;
    return status;
  }

  /* Enable accelerometer and gyroscope in Low Noise mode. */
  status = ICM42688_WriteReg(dev, ICM42688_PWR_MGMT0,
                             (uint8_t)(ICM42688_PWR_ACCEL_LN | ICM42688_PWR_GYRO_LN));
  if (status != ICM42688_OK)
  {
    dev->init_status = status;
    return status;
  }

  /* Gyroscope needs tens of ms to stabilise after power-on (datasheet). */
  HAL_Delay(ICM42688_STARTUP_DELAY_MS);

  dev->init_status = ICM42688_OK;
  return ICM42688_OK;
}

ICM42688_Status ICM42688_ReadRaw(ICM42688_Handle *dev, ICM42688_RawData *raw)
{
  uint8_t buf[ICM42688_RAW_DATA_LEN];
  ICM42688_Status status;

  if ((dev == 0) || (raw == 0))
  {
    return ICM42688_ERR_READ;
  }

  /* Reads 12 consecutive bytes:
     ACCEL X1 X0 Y1 Y0 Z1 Z0  GYRO X1 X0 Y1 Y0 Z1 Z0 */
  status = ICM42688_ReadRegs(dev, ICM42688_ACCEL_DATA_X1, buf, ICM42688_RAW_DATA_LEN);
  if (status != ICM42688_OK)
  {
    return status;
  }

  raw->ax = combine_u8(buf[0],  buf[1]);
  raw->ay = combine_u8(buf[2],  buf[3]);
  raw->az = combine_u8(buf[4],  buf[5]);
  raw->gx = combine_u8(buf[6],  buf[7]);
  raw->gy = combine_u8(buf[8],  buf[9]);
  raw->gz = combine_u8(buf[10], buf[11]);

  return ICM42688_OK;
}

ICM42688_Status ICM42688_ReadData(ICM42688_Handle *dev,
                                  ICM42688_RawData *raw,
                                  ICM42688_Data *data)
{
  ICM42688_Status status;

  if ((dev == 0) || (raw == 0) || (data == 0))
  {
    return ICM42688_ERR_READ;
  }

  status = ICM42688_ReadRaw(dev, raw);
  if (status != ICM42688_OK)
  {
    return status;
  }

  const float accel_scale = ICM42688_ACCEL_FS_G / ICM42688_LSB_FULL_SCALE;
  const float gyro_scale  = ICM42688_GYRO_FS_DPS / ICM42688_LSB_FULL_SCALE;

  data->ax_g = (float)raw->ax * accel_scale;
  data->ay_g = (float)raw->ay * accel_scale;
  data->az_g = (float)raw->az * accel_scale;

  data->gx_dps = (float)raw->gx * gyro_scale;
  data->gy_dps = (float)raw->gy * gyro_scale;
  data->gz_dps = (float)raw->gz * gyro_scale;

  return ICM42688_OK;
}
