#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void battery_init(void);
uint16_t battery_read_mv(void);
uint8_t battery_percent_from_mv(uint16_t mv);
uint8_t battery_read_percent(void);

#ifdef __cplusplus
}
#endif
