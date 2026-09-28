/**
  ******************************************************************************
  * @file    w25q64.h
  * @brief   W25Q64JV SPI NOR flash driver (SPI2, manual CS on PB12).
  *
  *          Commands verified against W25Q64JV datasheet Rev J:
  *            9Fh Read JEDEC ID, 05h Read Status-1, 06h Write Enable,
  *            20h Sector Erase (4KB), 02h Page Program (256B), 03h Read Data.
  ******************************************************************************
  */

#ifndef W25Q64_H
#define W25Q64_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "main.h"

#define W25Q64_PAGE_SIZE        256U
#define W25Q64_SECTOR_SIZE      4096U
#define W25Q64_CAPACITY_BYTES   (8UL * 1024UL * 1024UL)

#define W25Q64_JEDEC_MANUFACTURER  0xEFU
#define W25Q64_JEDEC_MEMORY_TYPE   0x40U
#define W25Q64_JEDEC_CAPACITY      0x17U

typedef enum
{
  W25Q64_OK = 0,
  W25Q64_ERR_SPI,
  W25Q64_ERR_TIMEOUT,
  W25Q64_ERR_PARAM,
  W25Q64_ERR_VERIFY
} W25Q64_Status;

extern SPI_HandleTypeDef hspi2;

W25Q64_Status W25Q64_Init(void);
W25Q64_Status W25Q64_ReadJEDECID(uint8_t *manufacturer, uint8_t *memory_type,
                                 uint8_t *capacity);
W25Q64_Status W25Q64_WriteEnable(void);
W25Q64_Status W25Q64_ReadStatus(uint8_t *status);
W25Q64_Status W25Q64_WaitBusy(uint32_t timeout_ms);
W25Q64_Status W25Q64_SectorErase(uint32_t address);
W25Q64_Status W25Q64_PageProgram(uint32_t address, const uint8_t *data, uint16_t len);
/* Programs one page and returns without waiting for the internal write to
   finish. Caller must wait before addressing the device again. */
W25Q64_Status W25Q64_PageProgramAsync(uint32_t address, const uint8_t *data,
                                      uint16_t len);
W25Q64_Status W25Q64_Read(uint32_t address, uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* W25Q64_H */
