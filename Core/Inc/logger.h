/**
  ******************************************************************************
  * @file    logger.h
  * @brief   Offline IMU logger: flash layout, record format, state machine.
  ******************************************************************************
  */

#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Flash layout (byte addresses inside W25Q64, 8 MB total) */
#define LOG_META_ADDR        0x000000UL   /* 4 KB metadata sector         */
#define LOG_DATA_ADDR        0x001000UL   /* IMU sample area starts here  */
#define LOG_TEST_ADDR        0x7E0000UL   /* dev self-test sector         */
#define LOG_MOCK_ADDR        0x7E1000UL   /* dev mock-test sector         */
#define LOG_MAX_DATA_BYTES   (8UL * 1024UL * 1024UL - LOG_DATA_ADDR)

#define LOG_MAGIC            0x494D554CUL  /* "IMUL" */
#define LOG_VERSION          1UL

#define LOG_SAMPLE_RATE_HZ   500UL
#define LOG_TOTAL_SAMPLES    5000UL        /* 10 s at 500 Hz */
#define LOG_PAGE_SAMPLES     16UL          /* 16 x 16 B = 256 B page */
#define LOG_DATA_BYTES       (LOG_TOTAL_SAMPLES * 16UL)
#define LOG_DATA_SECTORS     ((LOG_DATA_BYTES + 4095UL) / 4096UL)

typedef struct __attribute__((packed))
{
  uint32_t time_us;

  int16_t ax;
  int16_t ay;
  int16_t az;

  int16_t gx;
  int16_t gy;
  int16_t gz;
} ImuSample;

_Static_assert(sizeof(ImuSample) == 16, "ImuSample must be exactly 16 bytes");

#ifdef LOGGER_IMU_DIAG
#define LOGGER_IMU_DIAG_EVENTS 32U
typedef struct
{
  uint32_t sample_index;
  uint32_t zero_run_length;
  uint32_t kind; /* 1 = all zero, 2 = all -1 */
  ImuSample sample;
  ImuSample previous_good;
  int32_t who_status;
  int32_t pwr_status;
  uint8_t who;
  uint8_t pwr;
} LoggerImuDiagEvent;

extern volatile uint32_t g_logger_diag_zero_total;
extern volatile uint32_t g_logger_diag_ff_total;
extern volatile uint32_t g_logger_diag_zero_run;
extern volatile uint32_t g_logger_diag_max_zero_run;
extern volatile uint32_t g_logger_diag_event_count;
extern LoggerImuDiagEvent g_logger_diag_events[LOGGER_IMU_DIAG_EVENTS];
#endif

typedef struct __attribute__((packed))
{
  uint32_t magic;      /* 0x494D554C = "IMUL" */
  uint32_t version;

  uint32_t sample_count;
  uint32_t sample_rate_hz;

  uint32_t record_size;      /* bytes of sample data */
  uint32_t duration_ms;

  uint32_t data_start_address;

  uint32_t data_crc32;       /* CRC32 over all sample bytes */
  uint32_t header_crc32;     /* CRC32 over this struct with this field = 0 */
} LogHeader;

typedef enum
{
  LOGGER_OK = 0,
  LOGGER_ERR_FLASH_ID,
  LOGGER_ERR_FLASH_TIMEOUT,
  LOGGER_ERR_FLASH_TEST,
  LOGGER_ERR_MOCK_TEST,
  LOGGER_ERR_IMU_ID,
  LOGGER_ERR_SPI,
  LOGGER_ERR_OVERFLOW,
  LOGGER_ERR_NO_LOG
} LoggerError;

typedef enum
{
  LOGGER_BOOT = 0,
  LOGGER_WAIT_COMMAND,
  LOGGER_ERASING,
  LOGGER_RECORDING,
  LOGGER_FINISHING,
  LOGGER_IDLE,
  LOGGER_DUMPING,
  LOGGER_ERROR
} LoggerState;

/* Debug-visible state. */
extern volatile uint32_t g_flash_jedec;
extern volatile uint8_t  g_jedec_reads[5][3];
extern volatile int      g_flash_test_result;
extern volatile int      g_mock_test_result;
extern volatile uint32_t g_flash_fail_offset;
extern volatile int      g_boot_test_result;

extern volatile LoggerState g_logger_state;
extern volatile LoggerError g_logger_error;
extern volatile uint32_t    g_log_samples;
extern volatile uint32_t    g_missed_samples;
extern volatile uint32_t    g_max_loop_us;
extern volatile uint32_t    g_log_duration_ms;
extern volatile int         g_log_valid;
extern volatile int         g_log_crc_ok;
/* Debugger-triggered command: 1=R 2=D 3=E 4=Z (0 = none) */
extern volatile uint32_t    g_cmd_request;

LoggerError Logger_Init(void);
void        Logger_Process(void);
void        Logger_OnSampleTick(void);   /* called from TIM6 ISR */
LoggerState Logger_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */
