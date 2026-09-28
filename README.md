# STM32G431CBU6 离线 IMU 记录器

基于 **STM32G431CBU6 + ICM-42688-P + W25Q64** 的离线六轴数据记录器。
脱离电脑用电池供电时，按一下按键即可 500 Hz 采样 10 秒并把数据写入板载 SPI Flash；
之后接上电脑，通过串口或 SWD 把记录导出成 CSV，并可用自带的 GUI 实时看姿态 / 回放 CSV。

## 功能特性

- **500 Hz 采样、连续 10 秒**（5000 个样本），TIM6 硬件定时，非 `HAL_Delay` 凑时序
- **双页缓冲 + 异步页写**，实测 0 丢点、最大任务延迟 2 µs
- 数据先落 **W25Q64**（80 KB / 20 个扇区），**掉电不丢**
- 完整记录后才写 metadata（magic + CRC32），中途断电不会被当成有效记录
- **Mahony** 姿态解算（加速度计 + 陀螺仪）
- 上电**陀螺零偏标定**，抑制偏航漂移
- 支持**按键 / 串口命令 / 调试器**三种触发方式
- 自带 **3D 实时姿态 GUI**、**CSV 导出**、**CSV 离线回放**工具

## 硬件连接

| 功能 | MCU 引脚 | 外设 |
|---|---|---|
| ICM-42688-P | PB3 SCK / PB4 MISO(SDO) / PB5 MOSI(SDI) / PB6 CS | SPI1（Mode 0，~664 kHz） |
| ICM-42688-P INT1 | PB7 | 暂未使用 |
| W25Q64JV | PB13 SCK / PB14 MISO / PB15 MOSI / PB12 CS | SPI2（Mode 0，~5.3 MHz） |
| 调试串口 | PA9 TX / PA10 RX | USART1，115200 8N1 |
| 用户按键 | PB10（默认，可在 `main.h` 改） | 低电平有效，内部上拉 |
| SWD | PA13 SWDIO / PA14 SWCLK | ST-Link |

注意：

- **PB4 / PB6 同时是 UCPD 的 CC2 / CC1**，复位后默认带约 5 kΩ 下拉，固件已在
  `HAL_Init()` 之后调用 `HAL_PWREx_DisableUCPDDeadBattery()` 关闭。
- IMU 模块是 5 V 供电经板载 LDO，不要只给 3.3 V。

## 目录结构

```
Core/                     应用与驱动
  Src/main.c              时钟、GPIO、SPI1 初始化
  Src/logger.c            录制状态机、双缓冲、metadata、UART 命令
  Src/icm42688.c          ICM-42688-P SPI 驱动
  Src/w25q64.c            W25Q64 SPI Flash 驱动
  Src/mahony.c            Mahony 姿态解算
  Src/uart_link.c         USART1 + printf 重定向
  Src/imu_diag.c          可选的 IMU 采样诊断
Drivers/                  STM32G4 HAL + CMSIS
docs/                     设计与问题记录
tools/                    上位机脚本与诊断工具
exports/                  示例导出数据
CMakeLists.txt            CMake 构建脚本
STM32G431CBU6_FLASH.ld    链接脚本（128 KB Flash / 32 KB RAM）
```

## 编译

需要的工具：

- `arm-none-eabi-gcc`（Arm GNU Toolchain）
- CMake ≥ 3.20
- Ninja

```bash
cmake --preset Debug
cmake --build --preset Debug
```

产物在 `build/Debug/`：`g431_imu.elf`、`.hex`、`.bin`、`.map`。

## 烧录与调试

```bash
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
  -c "adapter speed 1000; program build/Debug/g431_imu.elf verify reset exit"
```

VSCode 里已配置好任务与调试（`.vscode/tasks.json`、`launch.json`，使用 Cortex-Debug + OpenOCD）。

## 上电与录制流程

```
上电
 ├─ 初始化：SPI2/W25Q64 → JEDEC 校验 → SPI1/ICM42688 → WHO_AM_I 校验
 ├─ 陀螺零偏标定（静止 500 点 ≈ 1 s）
 ├─ 读取旧记录 metadata 并校验 CRC
 └─ 等待触发（WAIT_COMMAND，同时输出实时姿态）
      ├─ 短按按键  → 开始录制
      ├─ 长按按键  → 导出 CSV
      └─ 串口 R / D / E / Z / C 命令
```

录制阶段：

```
ERASING（先擦 20 个数据扇区 + metadata 扇区）
  → RECORDING（TIM6 500 Hz 置标志 → 主循环读 IMU → 双页缓冲 → 异步写 Flash）
  → FINISHING（补写残页 → 写 metadata + CRC32）
  → IDLE
```

### 命令

| 命令 | 作用 |
|---|---|
| `R` | 立即开始 10 秒录制 |
| `D` | 把已存记录导出为 CSV |
| `E` | 擦除 metadata（使旧记录失效） |
| `Z` | 姿态调零 + 重新标定陀螺零偏 |
| `C` | 只重新标定陀螺零偏 |

调试器也可以触发：写 `g_cmd_request = 1..4`（1=R 2=D 3=E 4=Z），不需要串口。

## 数据格式

单条样本 16 字节（`_Static_assert` 保证）：

```c
typedef struct __attribute__((packed)) {
    uint32_t time_us;   /* DWT 微秒时间戳 */
    int16_t  ax, ay, az;
    int16_t  gx, gy, gz;
} ImuSample;
```

Flash 布局：

| 区域 | 地址 |
|---|---|
| metadata（4 KB） | `0x000000` |
| 采样数据 | `0x001000`（10 秒占 20 个扇区 = 80 KB） |
| 开发自检扇区 | `0x7E0000` / `0x7E1000` |

metadata 含 `magic = 0x494D554C ("IMUL")`、版本、样本数、采样率、字节数、时长、
数据起始地址、数据 CRC32、header CRC32。

## 导出数据

**方式一：串口（需要 PA10 连接）**

```bash
python tools/dump_csv.py COM17 imu_log.csv
```

**方式二：SWD 只读导出（不依赖串口，已验证）**

烧录 `SWD_EXPORT_ONLY` 固件后：

```bash
gdb -q build/SwdExport/g431_imu.elf -x tools/export_swd_log.gdb
python tools/convert_swd_log.py
```

## 上位机 GUI

```bash
python tools/imu_gui.py
```

- 实时 3D 姿态（颜色约定：**红 = X，黑 = Y，绿 = Z**）
- ZERO 一键调零
- R / E / D 记录器按钮
- 加载 CSV 离线回放（内部做零偏扣除 + 安装轴重映射）

依赖：`pyserial`、`numpy`、`matplotlib`

## 已知问题与设计决策

详见 `docs/`：

- `docs/known-issues/imu-cs-setup-timing.md`
  ICM-42688-P 读回 `WHO_AM_I = 0x00` 的根因是 **CS 到第一个 SCLK 的建立时间不足**，
  修复方式是在驱动里加 CS 建立延时。附完整对照实验与回归检查清单。
- `docs/attitude-and-drift.md`
  姿态算法、漂移来源（陀螺零偏为主）、零偏标定、IMU 安装轴重映射、实测数据。
- `docs/logger-recording-logic.md`
  录制状态机、触发方式、Flash 布局、可调参数一览。

## 诊断工具

| 工具 | 用途 |
|---|---|
| `tools/bitbang_who.py` | 绕过 SPI 外设，用 GPIO 手动 bit-bang 读 WHO_AM_I |
| `tools/spi_reg_read.py` | 寄存器级 SPI1 读取，定位外设配置问题 |
| `tools/button_scan.py` | 扫描出用户按键接在哪个 GPIO |
| `tools/vcp_loopback.py` | ST-Link 虚拟串口自环测试 |
| `tools/read_raw_accel.gdb` | 通过 SWD 读一次原始加速度（判断安装方向） |

## 第三方代码

`Drivers/` 下为 STMicroelectronics 的 STM32G4 HAL 驱动与 CMSIS 组件，
遵循其各自的许可（BSD-3-Clause / Apache-2.0）。
