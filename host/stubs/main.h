/* Host stub for Core/Inc/main.h — no STM32 peripherals. */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

#define BOOT_MODE_APP  0
#define BOOT_MODE_WARM 1
#define BOOT_MODE_HOT  2

typedef enum {
    BSOD_ABORT,
    BSOD_HARDFAULT,
    BSOD_MEMFAULT,
    BSOD_BUSFAULT,
    BSOD_USAGEFAULT,
    BSOD_WATCHDOG,
    BSOD_OTHER,
    BSOD_COUNT,
} BSOD_t;

typedef enum {
    SDCARD_HW_UNDETECTED,
    SDCARD_HW_NO_SD_FOUND,
    SDCARD_HW_SPI1,
    SDCARD_HW_OSPI1,
} sdcard_hw_type_t;

extern sdcard_hw_type_t sdcard_hw_type;
extern RTC_HandleTypeDef hrtc;
extern OSPI_HandleTypeDef hospi1;
extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_spi1_tx;
extern DMA_HandleTypeDef hdma_spi1_rx;

#define PROFILING_INIT(_t_name)
#define PROFILING_MEASURE(_t)
#define PROFILING_START(_t_name)
#define PROFILING_END(_t_name)
#define PROFILING_DIFF(_t_name)

void Error_Handler(void);
void BSOD(BSOD_t fault, uint32_t pc, uint32_t lr) __attribute__((noreturn));
void boot_magic_set(uint32_t magic);
void SystemClock_Config(uint8_t new_oc_level);
void uptime_inc(void);
uint32_t uptime_get(void);
void wdog_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
