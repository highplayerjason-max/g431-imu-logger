/**
  ******************************************************************************
  * @file    logger.c
  * @brief   Offline IMU logger: TIM6 500 Hz sampling, double page buffer,
  *          10 s autonomous recording, metadata + CRC32, UART R/D/E/Z commands
  *          and CSV dump.
  ******************************************************************************
  */

#include <stdio.h>
#include <string.h>

#include "logger.h"
#include "w25q64.h"
#include "icm42688.h"
#include "mahony.h"
#include "uart_link.h"
#include "main.h"

/* Set to 1 to run the destructive Phase-3/4 self-tests at boot. */
#define LOGGER_RUN_BOOT_SELFTEST  0
/* Set to 1 so that, when a valid log exists, boot dumps it instead of
   starting a new recording. Useful when the target has no UART RX pin. */
#define LOGGER_AUTO_DUMP_ON_BOOT  0
/* 1 = record automatically 3 s after boot (original spec behaviour).
   0 = wait for the user button or a UART command instead. */
#define LOGGER_AUTO_RECORD_ON_BOOT 0

#define LOGGER_CMD_WINDOW_MS      3000U
#define LOGGER_LIVE_PERIOD_MS     20U
#define LOGGER_DUMP_SAMPLES       8U
#define LOGGER_GYRO_CAL_SAMPLES   500U   /* 500 x 2 ms = 1 s stationary */

/* User button: short press = start recording, long press = dump CSV. */
#define LOGGER_BTN_DEBOUNCE_MS    30U
#define LOGGER_BTN_LONG_MS        1500U

/* Gyro zero-bias measured at boot (raw counts) and applied to the attitude
   filter. Yaw drift is dominated by this bias: 1 dps of bias = 60 deg/min. */
volatile int32_t  g_gyro_bias_raw[3] = {0, 0, 0};
volatile uint32_t g_gyro_cal_samples = 0U;
volatile int32_t  g_gyro_bias_dps_x100[3] = {0, 0, 0};

/* W25Q64 JEDEC self-test storage */
volatile uint32_t g_flash_jedec       = 0U;
volatile uint8_t  g_jedec_reads[5][3] = {{0}};
volatile int      g_flash_test_result = -1;
volatile int      g_mock_test_result  = -1;
volatile uint32_t g_flash_fail_offset = 0U;
volatile int      g_boot_test_result  = -1;

/* Runtime state (debug visible) */
volatile LoggerState g_logger_state    = LOGGER_BOOT;
volatile LoggerError g_logger_error    = LOGGER_OK;
volatile uint32_t    g_log_samples     = 0U;
volatile uint32_t    g_missed_samples  = 0U;
volatile uint32_t    g_max_loop_us     = 0U;
volatile uint32_t    g_log_duration_ms = 0U;
volatile int         g_log_valid       = 0;
volatile int         g_log_crc_ok      = -1;
volatile uint32_t    g_cmd_request     = 0U;

volatile uint8_t who_am_i = 0xFFU;
Mahony_t         g_mahony;

TIM_HandleTypeDef htim6;

static ICM42688_Handle imu_handle;

static void calibrate_gyro_bias(void);

#ifdef LOGGER_IMU_DIAG
volatile uint32_t g_logger_diag_zero_total = 0U;
volatile uint32_t g_logger_diag_ff_total = 0U;
volatile uint32_t g_logger_diag_zero_run = 0U;
volatile uint32_t g_logger_diag_max_zero_run = 0U;
volatile uint32_t g_logger_diag_event_count = 0U;
LoggerImuDiagEvent g_logger_diag_events[LOGGER_IMU_DIAG_EVENTS];
static ImuSample logger_diag_previous_good;

static void logger_diag_capture(const ImuSample *sample, uint32_t kind)
{
  if (g_logger_diag_event_count >= LOGGER_IMU_DIAG_EVENTS)
  {
    return;
  }
  LoggerImuDiagEvent *event = &g_logger_diag_events[g_logger_diag_event_count++];
  event->sample_index = g_log_samples;
  event->zero_run_length = g_logger_diag_zero_run;
  event->kind = kind;
  event->sample = *sample;
  event->previous_good = logger_diag_previous_good;
  event->who_status = ICM42688_ReadReg(&imu_handle, ICM42688_WHO_AM_I_REG,
                                      &event->who);
  event->pwr_status = ICM42688_ReadReg(&imu_handle, ICM42688_PWR_MGMT0,
                                      &event->pwr);
}

static void logger_diag_sample(const ImuSample *sample)
{
  uint8_t all_zero = (sample->ax == 0 && sample->ay == 0 && sample->az == 0 &&
                      sample->gx == 0 && sample->gy == 0 && sample->gz == 0);
  uint8_t all_ff = (sample->ax == -1 && sample->ay == -1 && sample->az == -1 &&
                    sample->gx == -1 && sample->gy == -1 && sample->gz == -1);

  if (all_zero != 0U)
  {
    g_logger_diag_zero_total++;
    g_logger_diag_zero_run++;
    if (g_logger_diag_zero_run > g_logger_diag_max_zero_run)
    {
      g_logger_diag_max_zero_run = g_logger_diag_zero_run;
    }
    if (g_logger_diag_zero_run == 1U || (g_logger_diag_zero_run % 100U) == 0U)
    {
      logger_diag_capture(sample, 1U);
    }
  }
  else
  {
    g_logger_diag_zero_run = 0U;
    if (all_ff != 0U)
    {
      g_logger_diag_ff_total++;
      logger_diag_capture(sample, 2U);
    }
    else
    {
      logger_diag_previous_good = *sample;
    }
  }
}
#endif

/* Recording buffers */
static ImuSample page_a[LOG_PAGE_SAMPLES];
static ImuSample page_b[LOG_PAGE_SAMPLES];
static ImuSample *active_page  = page_a;
static ImuSample *standby_page = page_b;
static uint32_t   active_count = 0U;

static uint32_t record_flash_addr = LOG_DATA_ADDR;
static uint32_t record_crc        = 0xFFFFFFFFUL;
static uint32_t record_samples    = 0U;
static uint32_t record_start_ms   = 0U;

/* Time base */
static uint32_t cycles_per_us = 1U;
static uint32_t last_cyccnt   = 0U;
static uint32_t time_us_acc   = 0U;
static volatile uint32_t tick_cycles = 0U;
static volatile uint8_t  sample_due  = 0U;

/* State machine */
static LoggerState state       = LOGGER_BOOT;
static uint32_t    state_ts_ms = 0U;
static uint32_t    erase_index = 0U;
static uint32_t    erase_total = 0U;
static uint32_t    live_last_ms = 0U;

#if USER_BTN_ENABLED
/* User button debounce state (1 = released, 0 = pressed) */
static uint8_t  btn_raw        = 1U;
static uint8_t  btn_stable     = 1U;
static uint32_t btn_change_ms  = 0U;
static uint32_t btn_press_ms   = 0U;
static uint8_t  btn_long_fired = 0U;
volatile uint32_t g_btn_press_count = 0U;
volatile uint32_t g_btn_long_count  = 0U;
#endif

static LogHeader dump_header;
static uint32_t  dump_index = 0U;
static uint32_t  dump_crc   = 0xFFFFFFFFUL;
static uint8_t   dump_chunk[LOGGER_DUMP_SAMPLES * sizeof(ImuSample)];

/* ------------------------------------------------------------------------- */
static void set_state(LoggerState s)
{
  state = s;
  g_logger_state = s;
  state_ts_ms = HAL_GetTick();
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
  for (uint32_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (uint32_t b = 0; b < 8U; b++)
    {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
  }
  return crc;
}

static uint32_t crc32_final(uint32_t crc)
{
  return crc ^ 0xFFFFFFFFUL;
}

/* ------------------------------------------------------------------------- */
static void dwt_init(void)
{
  cycles_per_us = SystemCoreClock / 1000000UL;
  if (cycles_per_us == 0U)
  {
    cycles_per_us = 1U;
  }

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  last_cyccnt = DWT->CYCCNT;
  time_us_acc = 0U;
}

static uint32_t micros_now(void)
{
  uint32_t now = DWT->CYCCNT;
  uint32_t delta = now - last_cyccnt;   /* uint32 wrap-safe */
  last_cyccnt = now;
  time_us_acc += delta / cycles_per_us;
  return time_us_acc;
}

/* ------------------------------------------------------------------------- */
static void tim6_init(void)
{
  htim6.Instance               = TIM6;
  htim6.Init.Prescaler         = (uint32_t)(SystemCoreClock / 1000000UL) - 1U;
  htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim6.Init.Period            = 2000U - 1U;   /* 500 Hz */
  htim6.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    g_logger_error = LOGGER_ERR_SPI;
  }
}

static void tim6_start(void)
{
  __HAL_TIM_SET_COUNTER(&htim6, 0U);
  HAL_TIM_Base_Start_IT(&htim6);
}

static void tim6_stop(void)
{
  HAL_TIM_Base_Stop_IT(&htim6);
}

void Logger_OnSampleTick(void)
{
  if (sample_due != 0U)
  {
    g_missed_samples++;
  }
  sample_due = 1U;
  tick_cycles = DWT->CYCCNT;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    Logger_OnSampleTick();
  }
}

/* ------------------------------------------------------------------------- */
static int header_valid(const LogHeader *h, int check_crc)
{
  if (h->magic != LOG_MAGIC || h->version != LOG_VERSION)
  {
    return 0;
  }
  if (h->sample_rate_hz != LOG_SAMPLE_RATE_HZ)
  {
    return 0;
  }
  if (h->data_start_address != LOG_DATA_ADDR)
  {
    return 0;
  }
  if (h->sample_count == 0U || h->sample_count > LOG_TOTAL_SAMPLES)
  {
    return 0;
  }
  if (check_crc != 0)
  {
    LogHeader tmp = *h;
    tmp.header_crc32 = 0U;
    uint32_t crc = crc32_final(crc32_update(0xFFFFFFFFUL, (const uint8_t *)&tmp,
                                            sizeof(LogHeader)));
    if (crc != h->header_crc32)
    {
      return 0;
    }
  }
  return 1;
}

/* Re-read the whole sample area and verify the stored CRC32. */
static int verify_log_crc(const LogHeader *h)
{
  uint8_t chunk[256];
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t remaining = h->record_size;
  uint32_t addr = h->data_start_address;

  while (remaining > 0U)
  {
    uint32_t n = (remaining > sizeof(chunk)) ? sizeof(chunk) : remaining;
    if (W25Q64_Read(addr, chunk, n) != W25Q64_OK)
    {
      return 0;
    }
    crc = crc32_update(crc, chunk, n);
    addr += n;
    remaining -= n;
  }

  return (crc32_final(crc) == h->data_crc32) ? 1 : 0;
}

static LoggerError flash_jedec_check(void)
{
  uint8_t mf = 0, mt = 0, cap = 0;

  for (uint32_t i = 0; i < 5U; i++)
  {
    W25Q64_Status st = W25Q64_ReadJEDECID(&mf, &mt, &cap);
    if (st != W25Q64_OK)
    {
      return LOGGER_ERR_SPI;
    }
    g_jedec_reads[i][0] = mf;
    g_jedec_reads[i][1] = mt;
    g_jedec_reads[i][2] = cap;
    HAL_Delay(1U);
  }

  g_flash_jedec = ((uint32_t)mf << 16) | ((uint32_t)mt << 8) | cap;

  if ((mf != W25Q64_JEDEC_MANUFACTURER) ||
      (mt != W25Q64_JEDEC_MEMORY_TYPE) ||
      (cap != W25Q64_JEDEC_CAPACITY))
  {
    return LOGGER_ERR_FLASH_ID;
  }
  return LOGGER_OK;
}

/* ------------------------------------------------------------------------- */
static void start_recording(void)
{
#ifdef LOGGER_IMU_DIAG
  g_logger_diag_zero_total = 0U;
  g_logger_diag_ff_total = 0U;
  g_logger_diag_zero_run = 0U;
  g_logger_diag_max_zero_run = 0U;
  g_logger_diag_event_count = 0U;
  memset(&logger_diag_previous_good, 0, sizeof(logger_diag_previous_good));
#endif
  record_flash_addr = LOG_DATA_ADDR;
  record_crc        = 0xFFFFFFFFUL;
  record_samples    = 0U;
  active_count      = 0U;
  active_page       = page_a;
  standby_page      = page_b;

  g_log_samples     = 0U;
  g_missed_samples  = 0U;
  g_max_loop_us     = 0U;
  g_log_duration_ms = 0U;
  g_log_valid       = 0;

  erase_index = 0U;
  erase_total = LOG_DATA_SECTORS + 1U;   /* data sectors + metadata sector */

  printf("RECORDING START (%lu samples @ %lu Hz)\r\n",
         (unsigned long)LOG_TOTAL_SAMPLES, (unsigned long)LOG_SAMPLE_RATE_HZ);
  set_state(LOGGER_ERASING);
}

static void erase_step(void)
{
  uint32_t addr = (erase_index < LOG_DATA_SECTORS)
                    ? (LOG_DATA_ADDR + erase_index * W25Q64_SECTOR_SIZE)
                    : LOG_META_ADDR;

  if (W25Q64_SectorErase(addr) != W25Q64_OK)
  {
    g_logger_error = LOGGER_ERR_FLASH_TIMEOUT;
    printf("ERASE FAIL at 0x%06lX\r\n", (unsigned long)addr);
    set_state(LOGGER_ERROR);
    return;
  }

  erase_index++;
  if (erase_index >= erase_total)
  {
    micros_now();
    record_start_ms = HAL_GetTick();
    tim6_start();
    set_state(LOGGER_RECORDING);
  }
}

static void program_page(ImuSample *page, uint32_t bytes)
{
  if (W25Q64_PageProgramAsync(record_flash_addr, (const uint8_t *)page,
                              (uint16_t)bytes) != W25Q64_OK)
  {
    g_logger_error = LOGGER_ERR_FLASH_TEST;
    set_state(LOGGER_ERROR);
    return;
  }
  record_flash_addr += bytes;
}

static void append_sample(const ImuSample *s)
{
  active_page[active_count] = *s;
  active_count++;
  record_crc = crc32_update(record_crc, (const uint8_t *)s, sizeof(ImuSample));

  record_samples++;
  g_log_samples = record_samples;

  if (record_samples >= LOG_TOTAL_SAMPLES)
  {
    set_state(LOGGER_FINISHING);
    return;
  }

  if (active_count >= LOG_PAGE_SAMPLES)
  {
    program_page(active_page, (uint32_t)LOG_PAGE_SAMPLES * sizeof(ImuSample));
    ImuSample *tmp = active_page;
    active_page = standby_page;
    standby_page = tmp;
    active_count = 0U;
  }
}

static void finish_recording(void)
{
  tim6_stop();

  if (active_count > 0U)
  {
    if (W25Q64_PageProgram(record_flash_addr, (const uint8_t *)active_page,
                           (uint16_t)(active_count * sizeof(ImuSample))) != W25Q64_OK)
    {
      g_logger_error = LOGGER_ERR_FLASH_TEST;
      set_state(LOGGER_ERROR);
      return;
    }
  }

  if (W25Q64_WaitBusy(2000U) != W25Q64_OK)
  {
    g_logger_error = LOGGER_ERR_FLASH_TIMEOUT;
    set_state(LOGGER_ERROR);
    return;
  }

  LogHeader h;
  memset(&h, 0, sizeof(h));
  h.magic              = LOG_MAGIC;
  h.version            = LOG_VERSION;
  h.sample_count       = record_samples;
  h.sample_rate_hz     = LOG_SAMPLE_RATE_HZ;
  h.record_size        = record_samples * (uint32_t)sizeof(ImuSample);
  h.duration_ms        = HAL_GetTick() - record_start_ms;
  h.data_start_address = LOG_DATA_ADDR;
  h.data_crc32         = crc32_final(record_crc);
  h.header_crc32       = 0U;
  h.header_crc32       = crc32_final(crc32_update(0xFFFFFFFFUL,
                                                  (const uint8_t *)&h,
                                                  sizeof(LogHeader)));

  if (W25Q64_PageProgram(LOG_META_ADDR, (const uint8_t *)&h,
                         (uint16_t)sizeof(LogHeader)) != W25Q64_OK)
  {
    g_logger_error = LOGGER_ERR_FLASH_TEST;
    set_state(LOGGER_ERROR);
    return;
  }
  if (W25Q64_WaitBusy(2000U) != W25Q64_OK)
  {
    g_logger_error = LOGGER_ERR_FLASH_TIMEOUT;
    set_state(LOGGER_ERROR);
    return;
  }

  g_log_duration_ms = h.duration_ms;
  g_log_valid       = 1;

  printf("RECORD COMPLETE\r\n");
  printf("Samples: %lu\r\n", (unsigned long)h.sample_count);
  printf("Duration: %lu ms\r\n", (unsigned long)h.duration_ms);
  printf("Bytes: %lu\r\n", (unsigned long)h.record_size);
  printf("Missed samples: %lu\r\n", (unsigned long)g_missed_samples);
  printf("Max loop latency: %lu us\r\n", (unsigned long)g_max_loop_us);
  printf("Flash end address: 0x%06lX\r\n", (unsigned long)record_flash_addr);

  set_state(LOGGER_IDLE);
}

/* ------------------------------------------------------------------------- */
static void begin_dump(void)
{
  if (W25Q64_Read(LOG_META_ADDR, (uint8_t *)&dump_header, sizeof(dump_header)) != W25Q64_OK)
  {
    printf("DUMP ERROR: metadata read failed\r\n");
    set_state(LOGGER_IDLE);
    return;
  }

  if (!header_valid(&dump_header, 1))
  {
    printf("DUMP ERROR: no valid log\r\n");
    set_state(LOGGER_IDLE);
    return;
  }

  dump_index = 0U;
  dump_crc   = 0xFFFFFFFFUL;

  printf("time_us,ax_raw,ay_raw,az_raw,gx_raw,gy_raw,gz_raw,"
         "ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps\r\n");
  set_state(LOGGER_DUMPING);
}

static void dump_step(void)
{
  uint32_t remaining = dump_header.sample_count - dump_index;
  uint32_t count = (remaining > LOGGER_DUMP_SAMPLES) ? LOGGER_DUMP_SAMPLES : remaining;
  uint32_t bytes = count * (uint32_t)sizeof(ImuSample);
  uint32_t addr = dump_header.data_start_address + dump_index * sizeof(ImuSample);

  if (W25Q64_Read(addr, dump_chunk, bytes) != W25Q64_OK)
  {
    printf("DUMP ERROR: flash read failed\r\n");
    set_state(LOGGER_IDLE);
    return;
  }
  dump_crc = crc32_update(dump_crc, dump_chunk, bytes);

  for (uint32_t i = 0; i < count; i++)
  {
    ImuSample s;
    memcpy(&s, dump_chunk + i * sizeof(ImuSample), sizeof(ImuSample));
    printf("%lu,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\r\n",
           (unsigned long)s.time_us,
           s.ax, s.ay, s.az, s.gx, s.gy, s.gz,
           (double)s.ax * (16.0 / 32768.0),
           (double)s.ay * (16.0 / 32768.0),
           (double)s.az * (16.0 / 32768.0),
           (double)s.gx * (2000.0 / 32768.0),
           (double)s.gy * (2000.0 / 32768.0),
           (double)s.gz * (2000.0 / 32768.0));
  }

  dump_index += count;
  if (dump_index >= dump_header.sample_count)
  {
    uint32_t crc = crc32_final(dump_crc);
    if (crc != dump_header.data_crc32)
    {
      printf("LOG CRC FAIL expected %08lX actual %08lX\r\n",
             (unsigned long)dump_header.data_crc32, (unsigned long)crc);
    }
    printf("LOG_END\r\n");
    set_state(LOGGER_IDLE);
  }
}

/* ------------------------------------------------------------------------- */
static void idle_telemetry(void)
{
  uint32_t now = HAL_GetTick();
  if ((uint32_t)(now - live_last_ms) < LOGGER_LIVE_PERIOD_MS)
  {
    return;
  }
  live_last_ms = now;

  ICM42688_RawData raw;
  ICM42688_Data data;
  if (ICM42688_ReadData(&imu_handle, &raw, &data) != ICM42688_OK)
  {
    return;
  }

  static uint32_t last_us = 0U;
  uint32_t now_us = micros_now();
  float dt = (float)(now_us - last_us) * 1e-6f;
  last_us = now_us;
  if (dt <= 0.0f || dt > 0.5f)
  {
    dt = 0.02f;
  }

  /* Remove the boot-time gyro zero bias; this is what keeps yaw from
     drifting (1 dps of bias = 60 deg/min of apparent yaw rate). */
  const float dps_per_lsb = 2000.0f / 32768.0f;
  const float gx_s = data.gx_dps - (float)g_gyro_bias_raw[0] * dps_per_lsb;
  const float gy_s = data.gy_dps - (float)g_gyro_bias_raw[1] * dps_per_lsb;
  const float gz_s = data.gz_dps - (float)g_gyro_bias_raw[2] * dps_per_lsb;

  /*
   * IMU mounting remap: the sensor is mounted vertically on this board
   * (board flat -> sensor -Y points up, measured ay ~= -2057 counts).
   * Display frame requested by the user:
   *   display X = sensor X
   *   display Y = sensor Z
   *   display Z = -sensor Y   (sign keeps the frame right-handed, so a
   *                            board lying flat reads as level)
   */
  const float ax = data.ax_g;
  const float ay = data.az_g;
  const float az = -data.ay_g;
  const float gx = gx_s;
  const float gy = gz_s;
  const float gz = -gy_s;

  Mahony_Update(&g_mahony,
                gx * 0.017453292f,
                gy * 0.017453292f,
                gz * 0.017453292f,
                ax, ay, az, dt);

  printf("Q %.4f %.4f %.4f %.4f\r\n",
         (double)g_mahony.q0, (double)g_mahony.q1,
         (double)g_mahony.q2, (double)g_mahony.q3);
  printf("R %.2f %.2f %.2f\r\n",
         (double)g_mahony.roll, (double)g_mahony.pitch, (double)g_mahony.yaw);
}

/* ------------------------------------------------------------------------- */
static void handle_command(uint8_t ch)
{
  if (ch >= 'a' && ch <= 'z')
  {
    ch = (uint8_t)(ch - 'a' + 'A');
  }

  switch (ch)
  {
    case 'R':
      if ((state == LOGGER_WAIT_COMMAND || state == LOGGER_IDLE) &&
          g_flash_jedec != 0U && who_am_i == ICM42688_WHO_AM_I_EXPECTED)
      {
        start_recording();
      }
      break;
    case 'D':
      if (state == LOGGER_WAIT_COMMAND || state == LOGGER_IDLE)
      {
        begin_dump();
      }
      break;
    case 'E':
      if ((state == LOGGER_WAIT_COMMAND || state == LOGGER_IDLE) &&
          W25Q64_SectorErase(LOG_META_ADDR) == W25Q64_OK)
      {
        g_log_valid = 0;
        printf("LOG ERASED\r\n");
      }
      break;
    case 'Z':
      Mahony_Init(&g_mahony, 0.5f, 0.0f);
      calibrate_gyro_bias();
      printf("IMU ZEROED\r\n");
      break;
    case 'C':
      calibrate_gyro_bias();
      break;
    default:
      break;
  }
}

static void poll_uart(void)
{
  while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) != RESET)
  {
    uint8_t ch = (uint8_t)(huart1.Instance->RDR & 0xFFU);
    handle_command(ch);
  }
}

#if USER_BTN_ENABLED
static void poll_button(void)
{
  const uint32_t now = HAL_GetTick();
  const uint8_t level = (HAL_GPIO_ReadPin(USER_BTN_GPIO_Port, USER_BTN_Pin) == GPIO_PIN_RESET)
                            ? 0U : 1U;   /* 0 = pressed (active low) */

  if (level != btn_raw)
  {
    btn_raw = level;
    btn_change_ms = now;
  }
  else if (((uint32_t)(now - btn_change_ms) >= LOGGER_BTN_DEBOUNCE_MS) &&
           (btn_stable != level))
  {
    btn_stable = level;
    if (btn_stable == 0U)
    {
      btn_press_ms = now;
      btn_long_fired = 0U;
      g_btn_press_count++;
    }
    else if (btn_long_fired == 0U)
    {
      /* Short press: start a new recording. */
      handle_command('R');
    }
  }

  if ((btn_stable == 0U) && (btn_long_fired == 0U) &&
      ((uint32_t)(now - btn_press_ms) >= LOGGER_BTN_LONG_MS))
  {
    btn_long_fired = 1U;
    g_btn_long_count++;
    /* Long press: dump the stored log as CSV over UART. */
    handle_command('D');
  }
}
#endif

/* ------------------------------------------------------------------------- */
static void calibrate_gyro_bias(void)
{
  int64_t sum[3] = {0, 0, 0};
  uint32_t n = 0U;

  for (uint32_t i = 0U; i < LOGGER_GYRO_CAL_SAMPLES; i++)
  {
    ICM42688_RawData raw;
    if (ICM42688_ReadRaw(&imu_handle, &raw) == ICM42688_OK)
    {
      sum[0] += raw.gx;
      sum[1] += raw.gy;
      sum[2] += raw.gz;
      n++;
    }
    HAL_Delay(2U);
  }

  if (n > 0U)
  {
    for (uint32_t a = 0U; a < 3U; a++)
    {
      g_gyro_bias_raw[a] = (int32_t)(sum[a] / (int64_t)n);
      g_gyro_bias_dps_x100[a] =
          (int32_t)((float)g_gyro_bias_raw[a] * (2000.0f / 32768.0f) * 100.0f);
    }
  }
  g_gyro_cal_samples = n;

  printf("GYRO CAL: %lu samples, bias x100 dps = %ld %ld %ld\r\n",
         (unsigned long)n,
         (long)g_gyro_bias_dps_x100[0],
         (long)g_gyro_bias_dps_x100[1],
         (long)g_gyro_bias_dps_x100[2]);
}

LoggerError Logger_Init(void)
{
  set_state(LOGGER_BOOT);

  if (W25Q64_Init() != W25Q64_OK)
  {
    g_logger_error = LOGGER_ERR_SPI;
    set_state(LOGGER_ERROR);
    return LOGGER_ERR_SPI;
  }

  LoggerError err = flash_jedec_check();
  printf("W25Q64 JEDEC: %02X %02X %02X\r\n",
         (unsigned)g_jedec_reads[0][0], (unsigned)g_jedec_reads[0][1],
         (unsigned)g_jedec_reads[0][2]);
  g_boot_test_result = (int)err;
  if (err != LOGGER_OK)
  {
    printf("W25Q64 JEDEC FAIL, logger disabled\r\n");
    g_logger_error = err;
    set_state(LOGGER_ERROR);
    return err;
  }

  dwt_init();
  tim6_init();

  imu_handle.hspi    = &hspi1;
  imu_handle.cs_port = IMU_CS_GPIO_Port;
  imu_handle.cs_pin  = IMU_CS_Pin;

  ICM42688_Status imu_err = ICM42688_Init(&imu_handle);
  who_am_i = imu_handle.who_am_i;
  printf("ICM42688 WHO_AM_I = 0x%02X (status %d)\r\n",
         (unsigned)who_am_i, (int)imu_err);
  if (imu_err != ICM42688_OK)
  {
    g_logger_error = LOGGER_ERR_IMU_ID;
    set_state(LOGGER_ERROR);
    return LOGGER_ERR_IMU_ID;
  }

  Mahony_Init(&g_mahony, 0.5f, 0.0f);
  calibrate_gyro_bias();

  LogHeader h;
  if (W25Q64_Read(LOG_META_ADDR, (uint8_t *)&h, sizeof(h)) == W25Q64_OK)
  {
    g_log_valid = header_valid(&h, 1);
    if (g_log_valid)
    {
      g_log_crc_ok = verify_log_crc(&h);
      printf("Stored log: %lu samples, %lu ms, CRC %s\r\n",
             (unsigned long)h.sample_count, (unsigned long)h.duration_ms,
             g_log_crc_ok ? "OK" : "FAIL");
    }
  }

  printf("Existing log valid: %d\r\n", g_log_valid);
  printf("Commands within 3 s: R=record  D=dump  E=erase  Z=zero\r\n");
  set_state(LOGGER_WAIT_COMMAND);
  return LOGGER_OK;
}

LoggerState Logger_GetState(void)
{
  return state;
}

/* ------------------------------------------------------------------------- */
void Logger_Process(void)
{
  uint32_t now = HAL_GetTick();
  (void)now;   /* only used by the optional auto-record / auto-dump paths */

  if (g_cmd_request != 0U)
  {
    uint32_t req = g_cmd_request;
    g_cmd_request = 0U;
    switch (req)
    {
      case 1U: handle_command('R'); break;
      case 2U: handle_command('D'); break;
      case 3U: handle_command('E'); break;
      case 4U: handle_command('Z'); break;
      default: break;
    }
  }

  poll_uart();
#if USER_BTN_ENABLED
  poll_button();
#endif

  switch (state)
  {
    case LOGGER_WAIT_COMMAND:
#ifndef LOGGER_IMU_DIAG
#if LOGGER_AUTO_RECORD_ON_BOOT
      if ((uint32_t)(now - state_ts_ms) >= LOGGER_CMD_WINDOW_MS)
      {
#if LOGGER_AUTO_DUMP_ON_BOOT
        if (g_log_valid)
        {
          begin_dump();
          break;
        }
#endif
        if (g_flash_jedec != 0U && who_am_i == ICM42688_WHO_AM_I_EXPECTED)
        {
          start_recording();
        }
      }
#endif
      /* While waiting for the button / UART command, keep streaming the
         live attitude so the GUI still shows something. */
      idle_telemetry();
#endif
      break;

    case LOGGER_ERASING:
      erase_step();
      break;

    case LOGGER_RECORDING:
      if (sample_due != 0U)
      {
        sample_due = 0U;

        uint32_t latency_us = (DWT->CYCCNT - tick_cycles) / cycles_per_us;
        if (latency_us > g_max_loop_us)
        {
          g_max_loop_us = latency_us;
        }

        ICM42688_RawData raw;
        if (ICM42688_ReadRaw(&imu_handle, &raw) == ICM42688_OK)
        {
          ImuSample s;
          s.time_us = micros_now();
          s.ax = raw.ax;
          s.ay = raw.ay;
          s.az = raw.az;
          s.gx = raw.gx;
          s.gy = raw.gy;
          s.gz = raw.gz;
#ifdef LOGGER_IMU_DIAG
          logger_diag_sample(&s);
#endif
          append_sample(&s);
        }
        else
        {
          g_logger_error = LOGGER_ERR_SPI;
        }
      }
      break;

    case LOGGER_FINISHING:
      finish_recording();
      break;

    case LOGGER_DUMPING:
      dump_step();
      break;

    case LOGGER_IDLE:
#if LOGGER_AUTO_DUMP_ON_BOOT
      if (g_log_valid && (uint32_t)(now - state_ts_ms) >= 5000U)
      {
        begin_dump();
        break;
      }
#endif
      idle_telemetry();
      break;

    case LOGGER_ERROR:
    default:
      break;
  }
}
