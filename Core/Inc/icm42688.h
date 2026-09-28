/**
  ******************************************************************************
  * @file    icm42688.h
  * @brief   Minimal polling SPI driver for TDK InvenSense ICM-42688-P.
  *
  *          Scope of this bring-up driver:
  *            - software CS on PB6
  *            - SPI1 Mode 0, 8-bit, MSB first, blocking transfers
  *            - no FIFO / DMA / INT1 / sensor fusion
  ******************************************************************************
  */

#ifndef ICM42688_H
#define ICM42688_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ------------------------------ Device basics ---------------------------- */
#define ICM42688_WHO_AM_I_REG       0x75U
#define ICM42688_WHO_AM_I_EXPECTED  0x47U

#define ICM42688_REG_BANK_SEL       0x76U
#define ICM42688_DEVICE_CONFIG      0x11U
#define ICM42688_PWR_MGMT0          0x4EU
#define ICM42688_GYRO_CONFIG0       0x4FU
#define ICM42688_ACCEL_CONFIG0      0x50U

#define ICM42688_ACCEL_DATA_X1      0x1FU  /* start of 12-byte accel+gyro block */
#define ICM42688_RAW_DATA_LEN       12U

/* Config field values (bank 0) */
#define ICM42688_ODR_1KHZ           6U

#define ICM42688_GYRO_FS_2000DPS    0U
#define ICM42688_ACCEL_FS_16G       0U

#define ICM42688_PWR_ACCEL_LN       (0x03U << 0U)  /* accel  Low Noise mode */
#define ICM42688_PWR_GYRO_LN        (0x03U << 2U)  /* gyro   Low Noise mode */

/* Scale factors for the selected full scale ranges:
   accel: +/-16 g    over +/-32767 counts  -> 16/32768 g/LSB
   gyro : +/-2000 dps over +/-32767 counts -> 2000/32768 dps/LSB */
#define ICM42688_ACCEL_FS_G          16.0f
#define ICM42688_GYRO_FS_DPS         2000.0f
#define ICM42688_LSB_FULL_SCALE      32768.0f

/* -------------------------------- Status -------------------------------- */
typedef enum
{
  ICM42688_OK = 0,
  ICM42688_ERR_SPI = -1,     /* HAL SPI transaction failed */
  ICM42688_ERR_ID = -2,      /* WHO_AM_I is not 0x47 */
  ICM42688_ERR_READ = -3,    /* register read failed       */
  ICM42688_ERR_WRITE = -4    /* register write failed      */
} ICM42688_Status;

/* ------------------------------ Data structs ----------------------------- */
typedef struct
{
  int16_t ax;
  int16_t ay;
  int16_t az;

  int16_t gx;
  int16_t gy;
  int16_t gz;
} ICM42688_RawData;

typedef struct
{
  float ax_g;
  float ay_g;
  float az_g;

  float gx_dps;
  float gy_dps;
  float gz_dps;
} ICM42688_Data;

/* -------------------------------- Handle --------------------------------- */
typedef struct
{
  SPI_HandleTypeDef *hspi;      /* SPI peripheral handle                    */
  GPIO_TypeDef      *cs_port;   /* GPIO port of manual chip-select          */
  uint16_t           cs_pin;    /* GPIO pin  of manual chip-select          */

  uint8_t            who_am_i;  /* value read back from WHO_AM_I (debug)    */
  ICM42688_Status    init_status;
} ICM42688_Handle;

/* ------------------------------- Functions ------------------------------- */
ICM42688_Status ICM42688_ReadReg(ICM42688_Handle *dev, uint8_t reg, uint8_t *value);
ICM42688_Status ICM42688_ReadRegs(ICM42688_Handle *dev, uint8_t reg,
                                   uint8_t *data, uint16_t len);
ICM42688_Status ICM42688_WriteReg(ICM42688_Handle *dev, uint8_t reg, uint8_t value);

ICM42688_Status ICM42688_Init(ICM42688_Handle *dev);
ICM42688_Status ICM42688_ReadRaw(ICM42688_Handle *dev, ICM42688_RawData *raw);
ICM42688_Status ICM42688_ReadData(ICM42688_Handle *dev,
                                  ICM42688_RawData *raw,
                                  ICM42688_Data *data);

#ifdef __cplusplus
}
#endif

#endif /* ICM42688_H */
