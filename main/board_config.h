#pragma once

#include "driver/gpio.h"

#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_I2C_SCL_GPIO      GPIO_NUM_19
#define BOARD_I2C_SDA_GPIO      GPIO_NUM_20
#define BOARD_I2C_CLK_HZ        400000

#define BOARD_WS2812_GPIO       GPIO_NUM_8
#define BOARD_WS2812_LED_COUNT  1

#define BOARD_BATTERY_ADC_GPIO  GPIO_NUM_5
#define BOARD_BATTERY_ADC_UNIT  ADC_UNIT_1
#define BOARD_BATTERY_ADC_CHAN  ADC_CHANNEL_5

/* LiPo divider: Li+ -> R1 -> GPIO5 (ADC) -> R2 -> GND
 * Vbat = Vadc * (R1 + R2) / R2 */
#define BOARD_BATTERY_R1_OHM         100000
#define BOARD_BATTERY_R2_OHM         100000
#define BOARD_BATTERY_DIVIDER_RATIO  ((float)(BOARD_BATTERY_R1_OHM + BOARD_BATTERY_R2_OHM) / (float)BOARD_BATTERY_R2_OHM)
#define BOARD_BATTERY_VREF_MV        3300
/* LiPo 0%/100% for percentage display and low-battery LED. */
#define BOARD_BATTERY_EMPTY_MV       3000
#define BOARD_BATTERY_FULL_MV        4200
/* Red LED at or below this charge level (~3.3 V on the scale above). */
#define BOARD_BATTERY_LOW_PCT        25
#define BOARD_BATTERY_SAMPLE_MS      2000

#define BOARD_DEVICE_NAME       "SpaceScroll"

#define BOARD_ENCODER_UPDATE_MS 5
#define BOARD_DIAL_REPORT_MS    5

/* Hi-res scroll: dial degrees per one full legacy scroll line (higher = slower). */
#define BOARD_SCROLL_DEG_PER_LINE  1.0f
/* Delay after BLE connect before enabling Resolution Multiplier feature report. */
#define BOARD_HID_HI_RES_ENABLE_MS 500
