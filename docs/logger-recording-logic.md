# 离线记录器：录制逻辑与触发方式

更新日期：2026-09-21

## 一、上电初始化 `Logger_Init()`

1. `W25Q64_Init()`：SPI2 + PB12 片选
2. JEDEC 连读 5 次，必须 `EF 40 17`；失败 → `LOGGER_ERROR`，不做任何擦写
3. `dwt_init()` 微秒计时；`tim6_init()` 500 Hz 定时器（未启动）
4. `ICM42688_Init()`：`WHO_AM_I` 必须 `0x47`；失败 → `LOGGER_ERROR`
5. `Mahony_Init()` 姿态解算初始化
6. **陀螺零偏标定** `calibrate_gyro_bias()`：静止采 500 点（2 ms × 500 = 1 s）
7. 读 metadata `0x000000`，校验 magic / version / header CRC；
   有有效旧记录时再整体回读 80 KB 校验数据 CRC（`g_log_crc_ok`）
8. 进入 `LOGGER_WAIT_COMMAND`，此状态下持续输出实时姿态 Q/R

## 二、触发录制的方式

| 方式 | 操作 | 说明 |
|---|---|---|
| 按键 | **短按**（< 1.5 s） | 开始录制 10 秒 |
| 按键 | **长按**（≥ 1.5 s） | 通过 UART 导出 CSV |
| UART | 发 `R` / `D` / `E` / `Z` / `C` | 同左，需要 PC→MCU 串口通 |
| 调试器 | 写 `g_cmd_request = 1~4` | 1=R 2=D 3=E 4=Z，绕过串口 |
| 自动 | 上电 3 秒无命令 | 由 `LOGGER_AUTO_RECORD_ON_BOOT` 控制，当前为 **0（关闭）** |

按键定义在 `Core/Inc/main.h`：

```c
#define USER_BTN_ENABLED    1
#define USER_BTN_Pin        GPIO_PIN_10   /* <-- 改成你板子上的实际引脚 */
#define USER_BTN_GPIO_Port  GPIOB
```

按键为**低电平有效 + 内部上拉 + 30 ms 软件消抖**。

## 三、录制流程

```
WAIT_COMMAND → ERASING → RECORDING → FINISHING → IDLE
```

- **ERASING**：先擦 20 个数据扇区（`0x001000` 起 80 KB）+ metadata 扇区，
  每次 `Logger_Process()` 擦一个，避免边采样边擦破坏时序
- **RECORDING**（500 Hz）：
  - TIM6 每 2 ms 中断，只置 `sample_due` 标志
  - 主循环读 12 字节 IMU 原始数据 → 组成 16 字节 `ImuSample`
  - 双缓冲：16 个样本（256 B）一页，满了异步写 Flash 并换页
  - 累计 CRC32、统计丢点与最大延迟
  - 满 5000 点（10 s）→ FINISHING
- **FINISHING**：停 TIM6 → 补写最后的残页 → 等 Flash 空闲 → 写 metadata
  （magic `IMUL`、样本数、500 Hz、字节数、时长、数据 CRC32、header CRC32）
  → `g_log_valid = 1` → IDLE

记录期间**不输出串口数据**，避免 printf 影响 500 Hz 时序。

## 四、读取数据

1. **UART D 命令**：分块读 Flash → 打印 CSV（raw + 物理单位）→ 结束打印 `LOG_END`
2. **SWD 导出**（不依赖串口，已验证）：
   `build/SwdExport` 固件 + `tools/export_swd_log.gdb` + `tools/convert_swd_log.py`
   → 生成 `exports/imu_log_swd.csv`

## 五、Flash 布局

| 区域 | 地址 |
|---|---|
| metadata（4 KB） | `0x000000` |
| 采样数据（10 s 用 20 扇区） | `0x001000` |
| 自检扇区 | `0x7E0000` / `0x7E1000` |

`ImuSample` = 16 字节：`uint32 time_us + int16 ax/ay/az/gx/gy/gz`。

## 六、可调参数

| 参数 | 位置 | 当前值 |
|---|---|---|
| 采样率 | `logger.h` `LOG_SAMPLE_RATE_HZ` | 500 Hz |
| 记录点数/时长 | `logger.h` `LOG_TOTAL_SAMPLES` | 5000（10 s） |
| 命令窗口 | `logger.c` `LOGGER_CMD_WINDOW_MS` | 3000 ms |
| 自动录制 | `logger.c` `LOGGER_AUTO_RECORD_ON_BOOT` | 0（关闭） |
| 自动导出 | `logger.c` `LOGGER_AUTO_DUMP_ON_BOOT` | 0（关闭） |
| 上电标定点数 | `logger.c` `LOGGER_GYRO_CAL_SAMPLES` | 500（1 s） |
| 按键消抖 / 长按 | `logger.c` `LOGGER_BTN_DEBOUNCE_MS` / `LOGGER_BTN_LONG_MS` | 30 ms / 1500 ms |
