/* Minimal STM32 HAL stand-in so firmware headers parse on Linux/macOS. */
#ifndef HOST_STM32H7XX_HAL_H
#define HOST_STM32H7XX_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef unsigned int uint;

typedef struct { uint32_t dummy; } SPI_HandleTypeDef;
typedef struct { uint32_t dummy; } LTDC_HandleTypeDef;
typedef struct { uint32_t dummy; } SAI_HandleTypeDef;
typedef struct { uint32_t dummy; } DMA_HandleTypeDef;
typedef struct { uint32_t dummy; } RTC_HandleTypeDef;
typedef struct { uint32_t dummy; } OSPI_HandleTypeDef;

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3,
} HAL_StatusTypeDef;

static inline uint32_t HAL_GetTick(void)
{
    /* Overridden at runtime via host_platform_ticks_ms when linked. */
    extern uint32_t host_platform_ticks_ms(void);
    return host_platform_ticks_ms();
}

#endif /* HOST_STM32H7XX_HAL_H */
