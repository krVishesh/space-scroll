#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void status_led_init(void);
void status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
void status_led_off(void);
void status_led_set_advertising(void);
void status_led_set_connected(void);
void status_led_set_low_battery(void);

#ifdef __cplusplus
}
#endif
