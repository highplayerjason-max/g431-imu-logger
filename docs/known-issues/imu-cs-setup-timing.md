# 已知问题：ICM-42688-P 读 WHO_AM_I = 0x00（CS 建立时间不足）

- 记录日期：2026-09-21
- 状态：**已定位、已修复、真机验证通过**
- 影响范围：所有使用 `Core/Src/icm42688.c` 的构建（修复前后的区别见下）

## 症状

- `ICM42688_Init()` 返回 `ICM42688_ERR_ID`，`who_am_i` 为 `0x00`（偶尔 `0xFF`），不是期望的 `0x47`。
- **时好时坏**：有时能正常采样，有时整段记录里出现大量全零样本（历史日志曾出现 983/5000 全零）。
- MCU 侧一切正常：SPI1 已使能、PB3/4/5 = AF5、PB6 = 输出且空闲高、模块供电 3.3V、四根线导通。
- 曾经误判为 PB15 虚焊、模块损坏、接线接反；补焊/换线后仍然复现。

## 根因

驱动在拉低 CS 之后**几乎立刻**开始 SPI 时钟。ICM-42688-P 数据手册要求 CS 建立时间 tSU.CS ≥ 39 ns；在本板上（模块 R2/R3/R4 4.7 kΩ 上拉 + 排针/走线电容）实际可用的时序余量"刚好不够"，传感器没有识别到这次选通，SDO 保持高阻，被模块 R6 10 kΩ 下拉拉到低电平，主机便读回 `0x00`。

由于只是"刚好不够"，温度、电压、线长、接触状态稍有变化就会表现为间歇性失败。

## 定位实验（同一块板、同一套线）

| 测试方法 | 结果 |
|---|---|
| 纯 GPIO bit-bang 读 WHO_AM_I（CS 拉低很久后才给时钟），连续 3 次 | `0x47 0x47 0x47` |
| 直接调用 `HAL_SPI_TransmitReceive()`，先手动拉低 CS | `0x47` |
| 驱动 `ICM42688_ReadReg()`（CS 拉低后立即发时钟） | `0x00` |
| 驱动 `ICM42688_ReadReg()`，先手动拉低 CS 再调用 | `0x47` |

前三组证明 IMU、供电、接线、SPI 外设都正常；唯一变量是 **CS 拉低到第一个时钟的间隔**。

## 修复

`Core/Src/icm42688.c` 的 `spi_transfer()`：拉低 CS 后插入一段建立时间延时，再调用
`HAL_SPI_TransmitReceive()`。

```c
HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);

/* CS setup time before the first SCLK edge (see docs/known-issues) */
for (volatile uint32_t cs_setup = 0U; cs_setup < 64U; cs_setup++)
{
  __NOP();
}

HAL_StatusTypeDef hal_status =
    HAL_SPI_TransmitReceive(dev->hspi, (uint8_t *)tx, rx, len, ICM42688_SPI_TIMEOUT);
```

64 次空循环在 170 MHz 下约 2~3 µs；500 Hz 采样时 CPU 占用 < 0.2%，对实时性无影响。

## 修复后真机验证

`build/ImuDiag`（20 秒 @ 500 Hz）：

```
g_imu_diag_init_status = 0
g_imu_diag_boot_who    = 0x47
g_imu_diag_samples     = 10000
g_imu_diag_zero_total  = 0        # 修复前同类日志为 983/5000
g_imu_diag_ff_total    = 0
g_imu_diag_hal_fail    = 0
g_imu_diag_event_count = 0
```

## 回归检查清单（以后再次出现 WHO_AM_I = 0 时按顺序执行）

1. 启动 openocd server，运行 `python tools/bitbang_who.py`：
   - 返回 `0x47` → IMU / 供电 / 接线正常，问题在 SPI 时序或配置；
   - 返回 `0x00` → 继续查硬件（供电、CS、MISO、模块焊接）。
2. 用示波器/逻辑分析仪看 CS 拉低到第一个 SCK 的间隔。
3. 万用表：模块 VDD/VDDIO = 3.3 V，CS 空闲 = 3.3 V，GND 共地。
4. 确认 `PWR_CR3` 的 `UCPD_DBDIS = 1`（PB4/PB6 的 UCPD dead-battery 下拉必须关闭）。
5. 确认当前构建包含本文档描述的 CS 建立时间修复。

## 相关工具

- `tools/bitbang_who.py`：绕过 SPI 外设，用 GPIO 手动 bit-bang 读 WHO_AM_I。
- `tools/spi_reg_read.py`：寄存器级 SPI1 读取（诊断外设配置）。

## 备注

- 修复已包含在 `build/ImuDiag`、`build/LoggerImuDiag`、`build/FixedLogger`。
- `build/Debug` 的 CMake 缓存仍指向旧路径 `C:\Users\Jason\Desktop\飞镖2026`，直接构建会失败，
  需要重新 configure（或使用 `build/FixedLogger`）。
