cmake_minimum_required(VERSION 3.20)

project(g431_imu LANGUAGES C ASM)

option(UART1_TEST_ONLY "Run standalone USART1 TX test instead of logger" OFF)
option(SWD_EXPORT_ONLY "Initialize W25Q64 for read-only SWD export" OFF)
option(IMU_DIAG_ONLY "Capture IMU SPI anomalies in RAM without touching W25Q64" OFF)
option(IMU_DIAG_FLASH_READ_STRESS "Add read-only SPI2 traffic during IMU diagnosis" OFF)
option(LOGGER_IMU_DIAG "Capture IMU zero-read diagnostics during a normal recording" OFF)
option(IMU_RESET_TIMING_TEST "UART-only IMU bank/reset timing test without W25 access" OFF)
option(UCPD_TEST_KEEP_ENABLED "A/B test A: leave UCPD dead-battery pull-down enabled" OFF)

if(IMU_DIAG_FLASH_READ_STRESS AND NOT IMU_DIAG_ONLY)
    message(FATAL_ERROR "IMU_DIAG_FLASH_READ_STRESS requires IMU_DIAG_ONLY")
endif()

if(UCPD_TEST_KEEP_ENABLED AND NOT IMU_RESET_TIMING_TEST)
    message(FATAL_ERROR "UCPD_TEST_KEEP_ENABLED requires IMU_RESET_TIMING_TEST")
endif()

if((IMU_RESET_TIMING_TEST AND (UART1_TEST_ONLY OR SWD_EXPORT_ONLY OR IMU_DIAG_ONLY OR LOGGER_IMU_DIAG)) OR
   (UART1_TEST_ONLY AND SWD_EXPORT_ONLY) OR
   (UART1_TEST_ONLY AND IMU_DIAG_ONLY) OR
   (SWD_EXPORT_ONLY AND IMU_DIAG_ONLY) OR
   (LOGGER_IMU_DIAG AND (UART1_TEST_ONLY OR SWD_EXPORT_ONLY OR IMU_DIAG_ONLY)))
    message(FATAL_ERROR "Only one standalone diagnostic mode can be enabled")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Cortex-M4F (single precision FPU) target flags
set(MCU_FLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfloat-abi=hard
    -mfpu=fpv4-sp-d16
)

set(COMMON_WARN_FLAGS
    -Wall
    -Wextra
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
)

# ----------------------------------------------------------------------------
# Application / HAL sources
# ----------------------------------------------------------------------------
set(SOURCES
    Core/Src/main.c
    Core/Src/icm42688.c
    Core/Src/mahony.c
    Core/Src/w25q64.c
    Core/Src/logger.c
    Core/Src/uart_link.c
    Core/Src/stm32g4xx_it.c
    Core/Src/stm32g4xx_hal_msp.c
    Core/Src/sysmem.c
    Core/Src/syscall.c

    Drivers/CMSIS/Device/ST/STM32G4xx/Source/system_stm32g4xx.c
    Drivers/CMSIS/Device/ST/STM32G4xx/Source/startup_stm32g431xx.s

    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_cortex.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_gpio.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr_ex.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc_ex.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_spi.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_spi_ex.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_tim.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_tim_ex.c
    Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_uart.c
)

if(IMU_DIAG_ONLY)
    list(APPEND SOURCES Core/Src/imu_diag.c)
endif()

# HAL core driver needs flash_ramfunc when FLASH module is enabled
list(APPEND SOURCES Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ramfunc.c)

set(INCLUDE_DIRS
    Core/Inc
    Drivers/CMSIS/Include
    Drivers/CMSIS/Device/ST/STM32G4xx/Include
    Drivers/STM32G4xx_HAL_Driver/Inc
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32G431xx
    $<$<CONFIG:Debug>:DEBUG>
)

if(UART1_TEST_ONLY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UART1_TEST_ONLY)
endif()

if(SWD_EXPORT_ONLY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SWD_EXPORT_ONLY)
endif()

if(IMU_DIAG_ONLY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE IMU_DIAG_ONLY)
endif()

if(IMU_DIAG_FLASH_READ_STRESS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE IMU_DIAG_FLASH_READ_STRESS)
endif()

if(LOGGER_IMU_DIAG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE LOGGER_IMU_DIAG)
endif()

if(IMU_RESET_TIMING_TEST)
    target_compile_definitions(${PROJECT_NAME} PRIVATE IMU_RESET_TIMING_TEST)
endif()

if(UCPD_TEST_KEEP_ENABLED)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UCPD_TEST_KEEP_ENABLED)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${INCLUDE_DIRS})

target_compile_options(${PROJECT_NAME} PRIVATE
    ${MCU_FLAGS}
    ${COMMON_WARN_FLAGS}
    $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>
    $<$<CONFIG:Debug>:-O0 -g3 -ggdb>
    $<$<CONFIG:Release>:-Os -g0>
)

target_link_options(${PROJECT_NAME} PRIVATE
    ${MCU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32G431CBU6_FLASH.ld
    -u _printf_float
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map
    -Wl,--print-memory-usage
)

target_link_libraries(${PROJECT_NAME} PRIVATE m c)

set_target_properties(${PROJECT_NAME} PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/STM32G431CBU6_FLASH.ld
)

# Post build: size report + hex/bin artifacts
find_program(ARM_SIZE_TOOL NAMES arm-none-eabi-size REQUIRED)
find_program(ARM_OBJCOPY_TOOL NAMES arm-none-eabi-objcopy REQUIRED)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${ARM_SIZE_TOOL} $<TARGET_FILE_NAME:${PROJECT_NAME}>
    COMMAND ${ARM_OBJCOPY_TOOL} -O ihex $<TARGET_FILE_NAME:${PROJECT_NAME}> ${PROJECT_NAME}.hex
    COMMAND ${ARM_OBJCOPY_TOOL} -O binary $<TARGET_FILE_NAME:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    VERBATIM
)
