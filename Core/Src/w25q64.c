/**
  ******************************************************************************
  * @file    w25q64.c
  * @brief   W25Q64JV SPI NOR flash driver.
  ******************************************************************************
  */

#include "w25q64.h"

#define W25Q64_CMD_WRITE_ENABLE   0x06U
#define W25Q64_CMD_READ_STATUS1   0x05U
#define W25Q64_CMD_READ_DATA      0x03U
#define W25Q64_CMD_PAGE_PROGRAM   0x02U
#define W25Q64_CMD_SECTOR_ERASE   0x20U
#define W25Q64_CMD_JEDEC_ID       0x9FU

#define W25Q64_SPI_TIMEOUT_MS     1000U

SPI_HandleTypeDef hspi2;

/* ------------------------------------------------------------------------- */
static void cs_low(void)
{
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
}

static void cs_high(void)
{
  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
}

static W25Q64_Status spi_tx(const uint8_t *data, uint16_t len)
{
  cs_low();
  HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi2, (uint8_t *)data, len, W25Q64_SPI_TIMEOUT_MS);
  cs_high();
  return (st == HAL_OK) ? W25Q64_OK : W25Q64_ERR_SPI;
}

static W25Q64_Status spi_tx_rx(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
  cs_low();
  HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx, rx, len,
                                                 W25Q64_SPI_TIMEOUT_MS);
  cs_high();
  return (st == HAL_OK) ? W25Q64_OK : W25Q64_ERR_SPI;
}

static void send_address(uint8_t *buf, uint32_t address)
{
  buf[1] = (uint8_t)((address >> 16) & 0xFFU);
  buf[2] = (uint8_t)((address >> 8) & 0xFFU);
  buf[3] = (uint8_t)(address & 0xFFU);
}

/* ------------------------------------------------------------------------- */
W25Q64_Status W25Q64_Init(void)
{
  hspi2.Instance               = SPI2;
  hspi2.Init.Mode              = SPI_MODE_MASTER;
  hspi2.Init.Direction         = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize          = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity       = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase          = SPI_PHASE_1EDGE;   /* Mode 0 */
  hspi2.Init.NSS               = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32; /* ~5.3 MHz @170MHz */
  hspi2.Init.FirstBit          = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode            = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial     = 7U;
  hspi2.Init.CRCLength         = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;

  cs_high();

  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    return W25Q64_ERR_SPI;
  }

  HAL_Delay(10U);
  return W25Q64_OK;
}

W25Q64_Status W25Q64_ReadJEDECID(uint8_t *manufacturer, uint8_t *memory_type,
                                 uint8_t *capacity)
{
  if ((manufacturer == 0) || (memory_type == 0) || (capacity == 0))
  {
    return W25Q64_ERR_PARAM;
  }

  uint8_t tx[4] = {W25Q64_CMD_JEDEC_ID, 0x00U, 0x00U, 0x00U};
  uint8_t rx[4] = {0};
  W25Q64_Status st = spi_tx_rx(tx, rx, 4U);
  if (st != W25Q64_OK)
  {
    return st;
  }

  *manufacturer = rx[1];
  *memory_type  = rx[2];
  *capacity     = rx[3];
  return W25Q64_OK;
}

W25Q64_Status W25Q64_WriteEnable(void)
{
  uint8_t cmd = W25Q64_CMD_WRITE_ENABLE;
  W25Q64_Status st = spi_tx(&cmd, 1U);
  if (st != W25Q64_OK)
  {
    return st;
  }

  uint8_t status = 0;
  st = W25Q64_ReadStatus(&status);
  if (st != W25Q64_OK)
  {
    return st;
  }

  return ((status & 0x02U) != 0U) ? W25Q64_OK : W25Q64_ERR_VERIFY;
}

W25Q64_Status W25Q64_ReadStatus(uint8_t *status)
{
  if (status == 0)
  {
    return W25Q64_ERR_PARAM;
  }

  uint8_t tx[2] = {W25Q64_CMD_READ_STATUS1, 0x00U};
  uint8_t rx[2] = {0};
  W25Q64_Status st = spi_tx_rx(tx, rx, 2U);
  if (st != W25Q64_OK)
  {
    return st;
  }

  *status = rx[1];
  return W25Q64_OK;
}

W25Q64_Status W25Q64_WaitBusy(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint8_t status = 0;

  do
  {
    W25Q64_Status st = W25Q64_ReadStatus(&status);
    if (st != W25Q64_OK)
    {
      return st;
    }
    if ((status & 0x01U) == 0U)
    {
      return W25Q64_OK;
    }
  } while ((uint32_t)(HAL_GetTick() - start) < timeout_ms);

  return W25Q64_ERR_TIMEOUT;
}

W25Q64_Status W25Q64_SectorErase(uint32_t address)
{
  if (address >= W25Q64_CAPACITY_BYTES)
  {
    return W25Q64_ERR_PARAM;
  }

  W25Q64_Status st = W25Q64_WriteEnable();
  if (st != W25Q64_OK)
  {
    return st;
  }

  uint8_t cmd[4] = {W25Q64_CMD_SECTOR_ERASE, 0, 0, 0};
  send_address(cmd, address);

  st = spi_tx(cmd, 4U);
  if (st != W25Q64_OK)
  {
    return st;
  }

  return W25Q64_WaitBusy(2000U);
}

W25Q64_Status W25Q64_PageProgram(uint32_t address, const uint8_t *data, uint16_t len)
{
  if ((data == 0) || (len == 0U) || (len > W25Q64_PAGE_SIZE))
  {
    return W25Q64_ERR_PARAM;
  }
  if ((address + len) > W25Q64_CAPACITY_BYTES)
  {
    return W25Q64_ERR_PARAM;
  }
  /* A page program must not cross a 256-byte page boundary. */
  if (((address & (W25Q64_PAGE_SIZE - 1U)) + len) > W25Q64_PAGE_SIZE)
  {
    return W25Q64_ERR_PARAM;
  }

  W25Q64_Status st = W25Q64_WriteEnable();
  if (st != W25Q64_OK)
  {
    return st;
  }

  uint8_t header[4] = {W25Q64_CMD_PAGE_PROGRAM, 0, 0, 0};
  send_address(header, address);

  cs_low();
  HAL_StatusTypeDef hal_st = HAL_SPI_Transmit(&hspi2, header, 4U, W25Q64_SPI_TIMEOUT_MS);
  if (hal_st == HAL_OK)
  {
    hal_st = HAL_SPI_Transmit(&hspi2, (uint8_t *)data, len, W25Q64_SPI_TIMEOUT_MS);
  }
  cs_high();

  if (hal_st != HAL_OK)
  {
    return W25Q64_ERR_SPI;
  }

  return W25Q64_WaitBusy(2000U);
}

W25Q64_Status W25Q64_PageProgramAsync(uint32_t address, const uint8_t *data,
                                      uint16_t len)
{
  if ((data == 0) || (len == 0U) || (len > W25Q64_PAGE_SIZE))
  {
    return W25Q64_ERR_PARAM;
  }
  if (((address & (W25Q64_PAGE_SIZE - 1U)) + len) > W25Q64_PAGE_SIZE)
  {
    return W25Q64_ERR_PARAM;
  }

  /* A previous program/erase must be finished before a new Write Enable. */
  W25Q64_Status st = W25Q64_WaitBusy(2000U);
  if (st != W25Q64_OK)
  {
    return st;
  }

  st = W25Q64_WriteEnable();
  if (st != W25Q64_OK)
  {
    return st;
  }

  uint8_t header[4] = {W25Q64_CMD_PAGE_PROGRAM, 0, 0, 0};
  send_address(header, address);

  cs_low();
  HAL_StatusTypeDef hal_st = HAL_SPI_Transmit(&hspi2, header, 4U, W25Q64_SPI_TIMEOUT_MS);
  if (hal_st == HAL_OK)
  {
    hal_st = HAL_SPI_Transmit(&hspi2, (uint8_t *)data, len, W25Q64_SPI_TIMEOUT_MS);
  }
  cs_high();

  return (hal_st == HAL_OK) ? W25Q64_OK : W25Q64_ERR_SPI;
}

W25Q64_Status W25Q64_Read(uint32_t address, uint8_t *data, uint32_t len)
{
  if ((data == 0) || (len == 0U) || ((address + len) > W25Q64_CAPACITY_BYTES))
  {
    return W25Q64_ERR_PARAM;
  }

  uint8_t header[4] = {W25Q64_CMD_READ_DATA, 0, 0, 0};
  send_address(header, address);

  cs_low();
  HAL_StatusTypeDef hal_st = HAL_SPI_Transmit(&hspi2, header, 4U, W25Q64_SPI_TIMEOUT_MS);
  if (hal_st == HAL_OK)
  {
    hal_st = HAL_SPI_Receive(&hspi2, data, (uint16_t)len, W25Q64_SPI_TIMEOUT_MS);
  }
  cs_high();

  return (hal_st == HAL_OK) ? W25Q64_OK : W25Q64_ERR_SPI;
}
