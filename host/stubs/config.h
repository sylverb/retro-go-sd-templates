/* Host stub for odroid config.h — drop ESP32 GPIO noise. */
#ifndef HOST_ODROID_CONFIG_H
#define HOST_ODROID_CONFIG_H

#ifndef RG_PATH_MAX
#define RG_PATH_MAX 255
#endif

/* RG_STORAGE_ROOT / ODROID_AUDIO_VOLUME_* come from the real config.h when
 * it is included; only provide GPIO stand-ins so that file can parse. */

#ifndef ODROID_SCREEN_WIDTH
#define ODROID_SCREEN_WIDTH  320
#endif
#ifndef ODROID_SCREEN_HEIGHT
#define ODROID_SCREEN_HEIGHT 240
#endif

/* Dummy pin ids so an unguarded vendor config.h can be parsed if included. */
#ifndef I2S_NUM_0
#define I2S_NUM_0 0
#endif
#ifndef GPIO_NUM_0
#define GPIO_NUM_0 0
#define GPIO_NUM_2 2
#define GPIO_NUM_4 4
#define GPIO_NUM_5 5
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_14 14
#define GPIO_NUM_15 15
#define GPIO_NUM_18 18
#define GPIO_NUM_19 19
#define GPIO_NUM_21 21
#define GPIO_NUM_22 22
#define GPIO_NUM_23 23
#define GPIO_NUM_25 25
#define GPIO_NUM_26 26
#define GPIO_NUM_27 27
#define GPIO_NUM_32 32
#define GPIO_NUM_33 33
#define GPIO_NUM_39 39
#endif
#ifndef ADC1_CHANNEL_6
#define ADC1_CHANNEL_6 6
#define ADC1_CHANNEL_7 7
#endif

#endif /* HOST_ODROID_CONFIG_H */
