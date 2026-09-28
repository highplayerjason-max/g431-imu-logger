#ifndef IMU_DIAG_H
#define IMU_DIAG_H

#include <stdint.h>

#define IMU_DIAG_EVENT_CAPACITY 64U
#define IMU_DIAG_SAMPLE_COUNT 10000U

typedef struct
{
  uint32_t sample_index;
  uint32_t time_us;
  uint32_t run_length;
  uint32_t spi_error;
  uint8_t kind; /* 1 = all zero, 2 = all FF, 3 = HAL failure */
  uint8_t hal_status;
  uint8_t rx[13];
  uint8_t last_good[12];
  uint8_t who;
  uint8_t who_status;
  uint8_t pwr;
  uint8_t pwr_status;
  uint8_t accel_cfg;
  uint8_t accel_status;
  uint8_t gyro_cfg;
  uint8_t gyro_status;
  uint8_t bank;
  uint8_t bank_status;
} ImuDiagEvent;

extern volatile uint32_t g_imu_diag_done;
extern volatile int32_t g_imu_diag_init_status;
extern volatile uint32_t g_imu_diag_boot_who;
extern volatile uint32_t g_imu_diag_samples;
extern volatile uint32_t g_imu_diag_zero_total;
extern volatile uint32_t g_imu_diag_ff_total;
extern volatile uint32_t g_imu_diag_hal_fail;
extern volatile uint32_t g_imu_diag_max_zero_run;
extern volatile uint32_t g_imu_diag_event_count;
extern volatile uint32_t g_imu_diag_flash_read_fail;
extern ImuDiagEvent g_imu_diag_events[IMU_DIAG_EVENT_CAPACITY];

void ImuDiag_Run(void);

#endif
