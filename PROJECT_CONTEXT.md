# STM32G431 独立 IMU 记录板项目上下文

## 2026-09-20 最新接线复测

- 复查用户文件夹中的 ICM 原厂规格书 `ICM42688P模块资料/芯片规格书/ICM-42688-P_规格书.PDF`：页 13 给芯片 VDD/VDDIO 各 1.71–3.6 V；页 14 上电到寄存器可访问为 1 ms、SPI 输入高阈值 0.7×VDDIO；页 52 明确 4 线 SPI 的 CS 低选中、高释放、未选中 SDO 高阻、MSB first、上升沿采样/下降沿改变、读命令首字节 bit7=1 且最少 16 时钟；页 63 `DEVICE_CONFIG.SPI_MODE=0` 默认支持 Mode0/3，软件复位写 bit0 后只需等 1 ms；页 87 WHO_AM_I `0x75` 复位值 `0x47`、REG_BANK_SEL 在所有 bank 可访问。当前读 `0xF5 + 0x00`、Mode0、约 664 kHz、20/5 ms 等待均与规格书相符，不能再以 SPI 模式/读帧/基础等待不足解释稳定 0。
- 模块原理图 `ICM42688P模块资料/原理图硬件/ICM42688P-1.1模块原理图.pdf`：H1 接口 pin1 标 `+5V`，pin2 GND；`+5V` 经 RT9193-33GB LDO 产生模块 `3V3`，再供给 ICM 的 VDD/VDDIO；R2/R3/R4 各 4.7 kΩ 从 MOSI/SCK/CS 到模块 `3V3`，R6 10 kΩ 将 MISO/AD0 下拉 GND。Richtek RT9193 本地规格书页 1/3 推荐 VIN 2.5–5.5 V，但标称 3.3 V 输出要有足够 VIN 余量；尚未实测当前模块 H1 pin1 电压、LDO 输出或 ICM VDDIO，ST-Link 目标 3.23 V 不能代替该测量。模块图也不能证明实物元件均已焊装。
- 厂商随附 STM32 示例 `ICM42688P模块资料/STM32示例程序/ICM42688P_SPI/Core/Src/icm42688p.c` 使用同样的 `reg|0x80`、2 字节全双工读取和 1 ms 软件复位等待；其 `writeRegister` 会再读回写入值，提示当前驱动的 HAL_OK 仅证明事务完成。当前按用户要求停止修改 SPI 时序/配置，下一步应测模块实际供电和四线波形。
- **UCPD 官方定义与 A/B 结果（最新）**：ST STM32G431x6/x8/xB datasheet DS12589 Rev6 页 60 注 6 明确 PB6=UCPD1_CC1、PB4=UCPD1_CC2；PA9=UCPD1_DBCC1 为高会启用 PB6 的约 5.1 kΩ 下拉，PA10=UCPD1_DBCC2 为高会启用 PB4 的约 5.1 kΩ 下拉。RM0440 `PWR_CR3` bit14 `UCPD1_DBDIS`：0 启用、1 关闭，复位后默认启用。本地 STM32G4 HAL 的 `HAL_PWREx_DisableUCPDDeadBattery()` 仅置该 bit；需先使能 PWR clock。当前正常代码原先在 `HAL_Init()` 返回后、时钟/GPIO/SPI/UART 初始化前调用；没有重新 enable 路径。`HAL_MspInit()` 在 `HAL_Init()` 内调用且已使能 PWR clock，因此适合试验更早关闭。
- 新增 `UCPD_TEST_KEEP_ENABLED` A/B 编译选项，仅用于 `IMU_RESET_TIMING_TEST`。A 跳过 `HAL_MspInit()` 中的 Disable，B 在 `HAL_MspInit()` 内调用 Disable；诊断流程、SPI 参数和引脚相同。正常固件仍保持原 `main()` 位置的 Disable；未改 logger/W25 源码，未擦写 W25。独立构建 `build/UcpdTestA`、`build/UcpdTestB` 均编译通过、烧入 MCU 内部 Flash 并 verify；UART 原始输出保存 `exports/ucpd_test_A.txt`、`exports/ucpd_test_B.txt`。
- **A 硬件实测（MCU reset）**：`PWR_CR3 before=after=0x00008000`、DBDIS=0；PB6 在 GPIO init 后=1、USART1 init 后=1，PA9=1、PA10=1；强制 Bank0 HAL状态0；软件复位前 WHO×10 全 `0x00`、每次 HAL状态0；software reset 写 HAL状态0；复位后 Bank0 写 HAL状态0；WHO×10 全 `0x00`、每次 HAL状态0。
- **B 硬件实测（MCU reset）**：`PWR_CR3 before=0x00008000, after=0x0000C000`、DBDIS=1；其余 PB6/PA9/PA10 与 WHO 结果与 A 完全相同。A/B **未使 WHO 恢复**，不能把 UCPD 确认为全零根因；但上方 reset-halt 中 PB6 低→关闭 DBDIS 后高的寄存器读数，仍证实 UCPD 下拉会影响复位窗口 PB6。当前板上运行 B 诊断固件。PB6 复位到 GPIO init 之间由硬件表征；固件未安全采样实际模拟电压。根据模块图 R4=4.7 kΩ 与芯片内部 5.1 kΩ 仅可计算理论分压约 1.72 V（假设 R4 实焊且下拉启用），尚无万用表实测，不可当实测电压。
- **下一步按用户边界**：停止修改初始化时序，测量 PB6/CS、PB3/SCK、PB5/MOSI、PB4/MISO 的实际 WHO(0x75) SPI 帧（MOSI 首字节应为 `0xF5`，约 16 个时钟，第二字节 MISO 应为 `0x47`）。若 CS/SCK/MOSI 正常但 MISO 始终低，再检查模块 SDO、VDDIO、连通/焊接。当前旧日志不可覆盖。
- 用户回复再次“已上电”后，未触发 MCU reset，直接读取 `ImuResetTiming` 的 RAM 冷启动记录：Bank0 写状态 0，ICM init -2，复位前 10 次与软件复位后 10 次 WHO 均 `0x00`，所有读状态 0；运行时 PB6 IDR 为高。用户尚未明确是否加装建议的 1 kΩ CS 上拉，不能把这次结果当成已完成外部上拉 A/B 对照。W25 未擦写。
- 用户按要求完成整板/IMU 断电再上电后，在不复位 MCU 的情况下通过 SWD 读出诊断 RAM：Bank0 写状态 0、`ICM init=-2`；软件复位前 10 次和复位后 10 次 WHO 均为 `0x00`、每次 HAL 状态 0。与此前仅 MCU reset 的 20 次结果相同；当前测试没有观察到冷热启动差异。
- 在 `reset halt`、程序尚未初始化 GPIO 时，PB6 复位模式为模拟输入（高阻），内部无上拉。暂时启用 GPIOB 时，PB6 作普通输入、无内部上拉读低；内部下拉读低，上拉读高。进一步在相同复位入口启用 PWR 时，`PWR_CR3=0x8000`（UCPD_DBDIS=0），PB6 低；仅置 `UCPD_DBDIS` 后变 `0xC000`，PB6 无内部上拉即读高。每次测试后 `reset run` 恢复固件。**实测**表明 STM32 的 UCPD dead-battery 下拉会在 reset 到 `HAL_PWREx_DisableUCPDDeadBattery()` 之间把 CS 拉低；模块原理图的 R4 4.7 kΩ 上拉至少在关闭该下拉后能使 PB6 为高，但 R4 当前焊装仍未目视/电阻确认。
- 本地 ICM datasheet `ICM42688P模块资料/芯片规格书/ICM-42688-P_规格书.PDF` 第 14 页给出上电后寄存器读写启动时间 1 ms；第 87 页确认 WHO_AM_I `0x75` 复位值 `0x47`、`REG_BANK_SEL 0x76` 在所有 bank 可访问且复位值 Bank0。当前 20 ms 上电等待和 5 ms 软件复位等待超过上述最低上电时间。复位窗口 CS 被拉低是实测现象，但尚不能证明它单独导致 WHO 全 0；即使软件运行后 CS 为高并强制 Bank0，读数仍是 0。
- 同一模块原理图还显示 MISO/AD0 有 R6 10 kΩ 到 GND 的下拉，因此此前 PB4 在内部弱上拉下仍低，不足以支持短路猜测；应以该设计解释静态读低。该证据修正上方旧排障笔记中的“PB4 可能短路”方向。
- 用户要求优先排查 IMU bank/复位时序，不改 Flash/logger、不要擦写 W25。搜索 `Core/Src/icm42688.c`：原先 `REG_BANK_SEL(0x76)` 仅写一次 `0x00`，无切换 Bank1–4 的写入路径。现驱动初始化先置 CS 高、等 20 ms、写 Bank0、等 2 ms、软件复位、等 5 ms、再写 Bank0、等 2 ms；`MX_GPIO_Init` 改为先写输出寄存器高，再切 GPIO 输出，避免初始化瞬间拉低。`logger.c` 和 W25 驱动未改。
- 新增独立 `IMU_RESET_TIMING_TEST` 构建 `build/ImuResetTiming/g431_imu.elf`，只初始化 SPI1/USART1，UART 打印软件复位前后各 10 次 WHO_AM_I；不进入 logger、不初始化/访问 W25。编译、烧入 MCU 内部 Flash、verify 均成功。COM17 实测：Bank0 写入 HAL 状态 0；复位前 10 次 WHO 全 `0x00`/状态 0；ICM init 状态 -2；复位后 10 次 WHO 全 `0x00`/状态 0。再次仅执行 MCU reset，得到完全相同的 20 次数据。注意 HAL 状态 0 仅表示 SPI 事务完成，不证明器件接收 Bank0 写入。
- 本地 `ICM42688P模块资料/原理图硬件/ICM42688P-1.1模块原理图.pdf` 页 1 显示 CS 到 3V3 的 R4 4.7 kΩ 上拉。它证明模块设计有上拉，但尚未核实当前实物模块 R4 是否焊装，或 MCU reset 空窗 PB6 实际电平。当前诊断固件留在 MCU 中，等待用户将所有供电完全断开再上电，以获取同一固件的冷启动 10 次 WHO 数据；不能把已有 MCU reset 结果当冷启动结果。W25 日志未擦写。
- 用户再次要求复试后，当前固件 SWD 读到错误仍为 IMU ID、WHO=`0x00`；在 COM17 已打开情况下复位，启动文本再次明确给出 `W25Q64 JEDEC: EF 40 17` 与 `ICM42688 WHO_AM_I = 0x00 (status -2)`。PB4 IDR 仍为低。未向 W25 擦写；需实测 IMU 供电/SDO 线路才能进一步定位。
- 在用户要求继续后，现有 `ImuDiag` 独立固件再次实测初始化状态 `-2`（ID 不匹配）、WHO=`0x00`，因此错误不局限于 logger 流程。已恢复并校验 `LoggerImuDiag`，当前仍是 IMU ID 错误，不会开始记录。
- SWD 只读寄存器显示 PB3/4/5 均为 AF5，PB6 为 GPIO 输出且高电平，SPI1 SR=`0x2`。PB4 输入空闲低；暂停 MCU 时临时将 PB4 切为 GPIO 输入并启用内部上拉，IDR 仍为低。随后立即恢复原 MODER/PUPDR 并继续运行。该结果提示 PB4/IMU SDO 线路存在外部下拉、短路或器件驱动低的可能，仍需断电电阻/通断和上电电压实测，不足以单独定位具体器件。没有对 W25 擦写。
- 用户报告已完成板子断电重启后，SWD 只读复查仍得到 JEDEC `EF 40 17`、`LOGGER_ERROR=7`、`LOGGER_ERR_IMU_ID=5`、`who_am_i=0x00`。由于初始化提前退出，`g_log_valid=0` 和 `g_log_crc_ok=-1` 是“未执行日志校验”，不是旧日志损坏证据。没有触发 W25Q64 擦写，当前不能新记录。
- 再次复测时，先打开 COM17（115200 8N1）再通过 SWD 复位独立 UART 测试固件，8 秒内收到 9 行 `UART1 TEST\r\n`，共 108 字节；当前 USART1 PA9→COM17 接收路径已实测通。此前两次 0 字节结果已被本次成功接收更新。
- 测试后已恢复并校验 `LoggerImuDiag` 固件，但两次复位后均读到 `LOGGER_ERROR=7`、`LOGGER_ERR_IMU_ID=5`、`who_am_i=0`；JEDEC 仍为 `EF 40 17`。由于 `Logger_Init` 在 IMU 初始化失败时提前返回，本次 `g_log_valid=0` 不能解释为 W25 旧日志失效，它没有执行旧日志读取/CRC。此次没有向 W25 擦写。IMU 通信当前需重新检查，不能沿用此前稳定 `0x47` 的结论。
- COM17 启动输出进一步确认 `ICM42688 WHO_AM_I = 0x00 (status -2)`，其中 -2 为驱动的 ID 不匹配错误；并非 UART 收不到启动输出。需检查 IMU 供电/CS/SPI 连线或完整断电重启后的读数，暂不开始新记录。
- 用户要求再次尝试后，重新烧录并校验独立 UART 测试固件；COM17 以 115200 8N1 监听 6 秒仍为 0 字节，SWD 读到发送计数 28、HAL 状态 0、PA9/PA10 AF7、USART1 CR1=`0xD`、BRR=`0x5C4`。已再次恢复并校验诊断固件，复读 JEDEC `EF 40 17`、WHO `0x47`、日志有效且状态为等待命令。仍需核对 PA9→实际 VCP/USB-TTL RX 与共地接线。
- ST-Link VCP 枚举为 COM17。烧录并校验 `build/UartTest/g431_imu.elf` 后，115200 8N1 监听 5 秒仍收到 0 字节；SWD 同时读到 `g_uart1_test_count=32`、`g_uart1_test_status=HAL_OK(0)`、PA9/PA10 AF7、USART1 CR1=`0xD`、BRR=`0x5C4`。说明固件定时调用发送且 HAL 返回成功，但尚未证明 PA9 到 COM17 VCP RX 的物理链路导通。应核对 PA9→VCP RX、GND→GND；SWCLK/SWDIO 不是 UART。
- 已恢复并校验 `build/LoggerImuDiag/g431_imu.elf`，复位后 SWD 读到状态 `LOGGER_WAIT_COMMAND=1`、错误 0、JEDEC `EF 40 17`、WHO_AM_I `0x47`、`g_log_valid=1`、`g_log_crc_ok=1`。该诊断构建不会自动记录；W25Q64 旧日志未擦写。
- 独立 IMU 20 秒 10000 次采样，以及每 16 次进行一次 W25 只读访问的 20 秒 10000 次采样，均未出现全零、全 FF 或 HAL 失败。旧日志中的 983 个全零样本原因仍未定位。记录期间事件捕获代码已准备，但启动新记录会覆盖板上旧日志，需用户明确同意；已校验备份 `exports/imu_log_swd.bin` 和 `.csv`。

更新：2026-09-20。以本目录当前源码和顶部本次 SWD 实测为准；下方另列用户提供的历史调试记录。

## 2026-09-20 IMU 全零诊断待板上运行

- 旧日志的 983 条六轴全零跨越 7 段，时间戳仍连续；静止不能解释加速度三轴也同时精确为零。`ICM42688_ReadRegs` 只要 HAL SPI 返回 `HAL_OK` 就接受收到的 12 字节，`ICM42688_ReadRaw` 和 logger 未做全零/全 FF 数据有效性检查。具体是 IMU/线路/供电还是传输状态导致，尚未由同期寄存器证实。
- 新增 `IMU_DIAG_ONLY` 构建模式及 `Core/Src/imu_diag.c`：只初始化 SPI1/ICM，按 500 Hz 采样 20 秒，把全零、全 FF、HAL 失败及异常时的 13 字节 SPI 响应、HAL 状态、WHO_AM_I、PWR_MGMT0、配置寄存器保存到 MCU RAM。**BUILD VERIFIED**（`build/ImuDiag`），不访问 W25Q64。
- 尝试烧入时 ST-Link 连续报 `open failed`，Windows 未列出 ST-Link/VCP，故诊断固件**尚未烧入，尚无新硬件诊断结果**。这两次失败发生在打开调试器之前，没有改动 MCU 内部 Flash；最后已验证板上运行的是 SWD 只读导出固件。需恢复 ST-Link USB 连接后再烧入并读 RAM 事件。

## 2026-09-20 SWD 只读导出完成

- 用户决定不用 UART，改为 SWD 只读导出 W25Q64 日志。已加入 `SWD_EXPORT_ONLY` 构建模式：只初始化 SPI2/W25Q64，读取 JEDEC 和日志头，之后等待 debugger 调用现有 `W25Q64_Read`。不进入 logger，也不调用外接 Flash 擦除/编程。
- 独立目录 `build/SwdExport` 编译成功。前两次 MCU 内部 Flash 烧写因 SWD 断连失败；按用户要求重试时，100 kHz 下使用 `init; halt; flash write_image erase; verify_image; reset run` 完整写入并逐字节验证了 11856 B。当前 MCU 运行的是 **SWD_EXPORT_ONLY 固件**，不运行 UART 测试或 logger。外接 W25Q64 未收到 erase/program 命令。
- 导出固件读取到 JEDEC `EF 40 17` 与原日志头：5000 条、500 Hz、80000 B、duration_ms 10001、data address `0x001000`。首次使用单次 4 KB 的读取结果 CRC 不匹配；同一段 4 KB 连读内容不稳定，但 MCU RAM 同一内容连读一致。改为现有 logger 校验所用的 256 B 小块后，前 4 KB 连读两遍一致。
- `tools/export_swd_log.gdb` 最终以 313 个小块经 SWD 读出完整数据；`tools/convert_swd_log.py` 核对长度、header CRC `EC746AD4` 和 data CRC `ABB69187` 均通过，然后生成 `exports/imu_log_swd.csv` 与原始 `exports/imu_log_swd.bin`。**HARDWARE VERIFIED (current)**：数据与外接 Flash 已存日志的 CRC 一致。没有通过 UART dump。
- `imu_gui.py` 的 CSV 解析函数成功读入全部 5000 行，离线 Mahony 计算生成 5000 个四元数；尚未手动操作 GUI 窗口做视觉检查。
- 全部 4999 个相邻时间戳差值均为 1999–2000 µs；但六轴原始值全为零的记录有 **983/5000（19.66%）**，分布在索引 2002–2013、2088–2150、2249–2665、2734–2933、4434–4441、4505–4760、4973–4999。索引 2001 的六轴值均为 -1。CRC 只证明存储/导出一致，不证明这些零值是有效传感器读数。首 10 条的合理性不能推及全程。GUI 可加载 CSV 回放，但零值区间的姿态变化不可视为真实运动证据。

## 2026-09-20 前 10 条日志与 USART1 独立测试

- 在烧入 UART 测试固件之前，GDB 暂停原 logger 固件，调用现有 `W25Q64_Read(0x001000, page_a, 160)`；返回 `W25Q64_OK=0`，按 `ImuSample[10]` 打印，并恢复原 `page_a` RAM 内容后继续运行。该调用只发读命令 `0x03`，未对 W25Q64 擦除或编程。**HARDWARE VERIFIED (current)**。

| # | time_us | ax | ay | az | gx | gy | gz |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 4225658 | -318 | -2022 | -41 | 5 | -17 | -44 |
| 1 | 4227657 | -318 | -2026 | -41 | 3 | -16 | -42 |
| 2 | 4229656 | -319 | -2028 | -37 | 4 | -16 | -37 |
| 3 | 4231656 | -318 | -2031 | -38 | 4 | -16 | -33 |
| 4 | 4233656 | -314 | -2030 | -38 | 6 | -16 | -26 |
| 5 | 4235656 | -317 | -2031 | -36 | 4 | -17 | -22 |
| 6 | 4237655 | -315 | -2029 | -37 | 4 | -17 | -14 |
| 7 | 4239655 | -317 | -2030 | -39 | 5 | -16 | -8 |
| 8 | 4241654 | -313 | -2018 | -35 | 5 | -16 | -2 |
| 9 | 4243654 | -315 | -2002 | -32 | 7 | -16 | 3 |

- 相邻 `time_us` 差值为 1999、1999、2000、2000、2000、1999、2000、1999、2000 µs。样本非零、非固定，三轴加速度幅值约 2047 raw count，符合当前 ±16 g 配置下约 1 g 的静态重力；陀螺仪小幅变化。它们看起来是真实 IMU 数据，但仅 10 条不能证明整个日志的采样质量或时间戳绝对起点。
- 现有 `Core/Src/main.c` 在正常构建中初始化 USART1；`Core/Src/uart_link.c` 的 `__io_putchar` 使用 `HAL_UART_Transmit(&huart1, ...)`。新增 CMake `UART1_TEST_ONLY` 选项。启用后固件只初始化时钟和 USART1，每秒直接调用 `HAL_UART_Transmit(&huart1, "UART1 TEST\r\n", ...)`，不进入 logger。
- 独立构建 `build/UartTest` 已编译并烧入 **MCU 内部 Flash**；OpenOCD 报编程和验证成功。W25Q64 未擦写。**BUILD VERIFIED / MCU PROGRAM VERIFIED**。目前板上运行的是 UART 测试固件，原 logger 固件镜像保存在 `build/ContextCheck/g431_imu.elf`/`.bin`，但不要在 UART 测试完成前恢复并启动 CSV dump。
- SWD 读到 `g_uart1_test_status=HAL_OK(0)`，`g_uart1_test_count` 约 2.2 秒内从 21 增至 23；GPIOA AFRH 为 `0x00000770`（PA9/PA10 AF7），USART1 CR1 为 `0x0000000D`（UE/TE/RE），BRR 为 `0x000005C4`（170 MHz 时约 115200 baud）。**HARDWARE VERIFIED (current, MCU 寄存器与发送调用)**。
- COM17 监听约 3 秒仍为 0 字节。尚无 PA9 引脚波形或 PA9→ST-Link VCP RX/USB-TTL RX 导通证据，故**不能声称物理 UART TX 路径已经验证**。下一步接线：PA9→接收器 RX、GND→GND；仅需发送时 PA10 不必连接。SWCLK/SWDIO 只用于调试，不是 UART。确认能看到每秒 `UART1 TEST` 后再考虑 CSV dump。

## 2026-09-20 当前 SWD 实测（优先于下方历史断点）

- 用户确认当前接线是调试器 **SWCLK/SWDIO**，并非 W25Q64 的 CLK/DI/DO。OpenOCD 通过 ST-Link V2 成功连接 Cortex-M4，目标电压约 3.23 V。**HARDWARE VERIFIED (current)**。
- 将当前 `build/ContextCheck/g431_imu.bin` 与 MCU 内部 Flash 的 61492 字节逐字节核对成功。该核对命令会占用 MCU 一段 RAM；随后已执行 `reset run` 恢复运行时初始化数据。没有对 MCU Flash 或 W25Q64 进行 program/erase。**HARDWARE VERIFIED (current)**。
- 重启后通过 SWD 读到 `g_flash_jedec = 0x00EF4017`，`g_jedec_reads[0..4]` 五组均为 `EF 40 17`，`g_boot_test_result = 0`。当前 W25Q64 JEDEC 通信 **HARDWARE VERIFIED (current)**；下方 `00 00 00` 是历史失败结果，不再是当前阻塞。
- 重启后 `who_am_i = 0x47`。当前 ICM WHO_AM_I 通信 **HARDWARE VERIFIED (current)**，但连续采样质量仍未单独验证。
- `g_log_valid = 1`、`g_log_crc_ok = 1`、`g_logger_error = 0`。已读出 RAM 中的日志头：magic `0x494D554C`、version 1、sample_count 5000、sample_rate_hz 500、record_size 80000 B、duration_ms 10001、data_start_address `0x001000`，data CRC32 `0xABB69187`、header CRC32 `0xEC746AD4`。这是当前固件对 W25Q64 存储日志的验证结果；本次尚未通过 UART 导出整份 CSV，也未检查每条样本的物理合理性。
- 当前状态曾读到 `LOGGER_WAIT_COMMAND=1`，3 秒后为 `LOGGER_DUMPING=6`，符合 `LOGGER_AUTO_DUMP_ON_BOOT=1` 且已有有效日志的代码路径。没有进入擦除/新记录状态。
- 本机 ST-Link VCP 枚举为 COM17；本次两次各监听约 3 秒均未收到字节。SWD 可读不证明 PA9 USART1_TX 已物理连接到 VCP RXD。整份日志导出仍需确认串口实际接线或设计只读 SWD 导出步骤。

**当前下一步**：优先确认 USART1 TX 到电脑接收端的物理路径，以便按现有固件导出 5000 条 CSV；如果目标只是核对通信，当前 JEDEC 与 WHO_AM_I 已通过。Flash pin 3/CS/线路电阻的历史排障步骤现在不再是默认下一步，除非通信再次失败。

## 标记

- **CODE VERIFIED**：已阅读当前源码，确认代码中的配置或逻辑。
- **BUILD VERIFIED**：在本机从独立构建目录完整编译并链接成功。
- **HARDWARE VERIFIED (historical)**：用户提供的既往真实硬件测量或运行结果；本次会话未复测。
- **NOT VERIFIED**：代码存在或计划存在，但没有成功的板上结果。

## 硬件架构与引脚

- MCU：STM32G431CBU6，HSI16 经 PLL 运行到 SYSCLK/HCLK/PCLK1/PCLK2 170 MHz。**CODE VERIFIED**：`Core/Src/main.c`。
- ICM-42688-P：PB3 SPI1_SCK、PB4 SPI1_MISO/SDO、PB5 SPI1_MOSI/SDI、PB6 手动 CS、PB7 INT1 暂未使用。**CODE VERIFIED**：`Core/Src/main.c`、`Core/Src/stm32g4xx_hal_msp.c`、`Core/Inc/main.h`。
- W25Q64JVSSIQ：PB12 手动 CS# -> pin 1；PB13 SPI2_SCK -> pin 6 CLK；PB14 SPI2_MISO -> pin 2 DO/IO1；PB15 SPI2_MOSI -> pin 5 DI/IO0。WP# pin 3、HOLD#/RESET# pin 7、CS# 均按用户硬件记录有 10 kΩ 上拉；VCC pin 8 为 3.3 V；C9 为 100 nF 去耦。后三项是用户给出的硬件描述，非代码验证。
- USART1：PA9 TX、PA10 RX，115200 8N1，printf 输出走 USART1。**CODE VERIFIED**：`Core/Src/uart_link.c`、`Core/Src/stm32g4xx_hal_msp.c`。ST-Link VCP 在本机枚举为 COM17，但 USART1 到 VCP 的实际连线与输出本次未验证。
- 板子亮、5 V/3.3 V 正常且 SWD 可连接：**HARDWARE VERIFIED (historical)**，用户提供。

## 当前代码状态

- 构建系统是 CMake + Ninja + arm-none-eabi-gcc，Debug/Release presets；VS Code `launch.json` 使用 Cortex-Debug + OpenOCD/ST-Link。**CODE VERIFIED**。
- SPI1：主机、双线、8 bit、MSB first、软件 NSS、Mode 0、170 MHz / 256 ≈ 664 kHz，PB3/4/5 AF5，PB6 GPIO 输出且空闲 HIGH。**CODE VERIFIED**。
- SPI2：主机、双线、8 bit、MSB first、软件 NSS、Mode 0、170 MHz / 32 ≈ 5.31 MHz，PB13/14/15 AF5，PB12 GPIO 输出且空闲 HIGH。**CODE VERIFIED**。
- ICM 驱动：`Core/Src/icm42688.c`、`Core/Inc/icm42688.h`。`ICM42688_Init` 真正读取 WHO_AM_I 并与 0x47 比较，不匹配即失败。**CODE VERIFIED**；成功通信 **NOT VERIFIED**。
- Flash 驱动：`Core/Src/w25q64.c`、`Core/Inc/w25q64.h`。JEDEC 函数一次 CS LOW/HIGH 中全双工发送 `[0x9F, 0, 0, 0]`，返回 `rx[1..3]`。`HAL_SPI_TransmitReceive` 非 `HAL_OK` 会返回错误；当前只保存成功/失败，不保存具体 HAL 状态或 `hspi2.ErrorCode`。**CODE VERIFIED**。
- `Core/Src/logger.c`、`Core/Inc/logger.h` 已存在，含 TIM6 500 Hz、双页缓冲、metadata/CRC、UART R/D/E/Z 命令和 CSV 导出。`ImuSample` 为 16 B 且有 `_Static_assert`。这些功能已实现于代码，但完整硬件功能 **NOT VERIFIED**。
- `LOGGER_RUN_BOOT_SELFTEST=0`，故启动不运行破坏性自测。`Logger_Init` 先读五次 JEDEC；只要 ID 错误就进入 `LOGGER_ERROR`，不进入 IMU 初始化、擦除或记录。只有 Flash 和 IMU 均通过后，才可能在命令或超时路径进入记录。**CODE VERIFIED**。
- `LOGGER_AUTO_DUMP_ON_BOOT=1`，有效旧日志会在命令窗口结束后自动导出；这与最初的“无命令直接记录”目标不同，属于当前代码的实际行为。**CODE VERIFIED**。

## 历史硬件结果与已解除的断点

- ICM WHO_AM_I 历史结果：SPI Mode 0 为 0xFF，Mode 3 为 0x00；预期 0x47。**HARDWARE VERIFIED (historical)**，当前 IMU 通信 **NOT VERIFIED**。
- W25Q64 JEDEC 历史连续五次为 `00 00 00`，预期 `EF 40 17`。**HARDWARE VERIFIED (historical)**；本次 SWD 已读到新运行结果，见顶部当前实测。Flash JEDEC 阻塞现已解除。
- Flash pin 8 VCC = 3.3 V、pin 7 HOLD#/RESET# = 3.3 V。**HARDWARE VERIFIED (historical)**。pin 3 WP# 电压、pin 1 CS# 空闲电压、断电时 PB14/Flash pin 2 到 GND 的电阻，以及四条 SPI 线的导通：**NOT VERIFIED**。
- Flash 未选中时 DO/MISO 为高阻；PB14 空闲 LOW 单独不能证明故障。真正需要观察选中时 MISO 数据。
- 当前代码未见能解释稳定 `00 00 00` 的明显 SPI2 配置或 0x9F 帧错误。可解释现象的候选包括选中期间 MISO 持续 LOW、CS/CLK/MOSI 实际未到芯片、接线/焊接/芯片问题；这些均未被证实。HAL 成功只证明 MCU 外设完成事务，不证明 Flash 返回正确数据。

## Flash 地址规划与最终目标

- `0x000000–0x000FFF` metadata sector；`0x001000` 起为采样数据。当前代码的自测 sector 为 `0x7E0000`、mock test sector 为 `0x7E1000`。**CODE VERIFIED**。JEDEC 未修复前不执行 erase/program/自测。
- 最终目标为 500 Hz × 10 s = 5000 个 16 B 采样，先擦所需 sector、TIM6 定时、主循环取样、RAM 双页缓冲、每 16 样本写一页、完成后写有效 metadata、掉电保存、UART 流式 CSV 导出。**目标；硬件完整验证未完成**。
- 当前 UART 命令：`R` 记录、`D` 导出、`E` 擦 metadata、`Z` 姿态归零。**CODE VERIFIED**。这些命令在通信问题排除前不要触发。

## 构建检查

- 2026-09-20：原 `build/Debug` 中的 CMakeCache 指向旧路径 `C:/Users/Jason/Desktop/飞镖2026`，在当前 `C:/Users/Jason/Desktop/RM/飞镖2026` 直接 `cmake --build --preset Debug --clean-first` 因缓存路径失效失败。
- 2026-09-20：使用相同 Debug preset 在独立的 `build/ContextCheck` 重新配置，并完整编译 29 个目标、链接 `g431_imu.elf` 成功，RAM 5416 B、MCU Flash 61492 B。**BUILD VERIFIED**。没有烧录或重新测板上结果。

## 历史 Flash 排障步骤（仅通信再次失败时使用）

1. 在 Flash 芯片脚上测 pin 3 WP# 对 GND 电压，以及 pin 1 CS# 空闲对 GND 电压；历史记录中的 pin 8/pin 7 无须无故重测。
2. 完全断电后量 PB14/Flash pin 2 到 GND 的实际电阻（Ω），并检查 PB12↔pin 1、PB14↔pin 2、PB15↔pin 5、PB13↔pin 6 导通。
3. 如果静态测量正常，使用逻辑分析仪或示波器在 0x9F 事务期间同时看 CS、CLK、MOSI、MISO；CS 应 LOW 包住 32 个时钟，MOSI 首字节为 0x9F，后续 MISO 应为 `EF 40 17`。
4. 如需隔离 PB14 线路问题，可临时将 SPI2 停用、PB14 配为 GPIO Input + Pull-Up 读取，再恢复 SPI2 AF5。此测试尚未实施，不应把内部上拉读数当作 JEDEC 成功证据。
5. JEDEC 读通后再处理 ICM WHO_AM_I；两个设备均通过后才验证 logger。每次真实硬件验证后更新本文件并标注证据。
