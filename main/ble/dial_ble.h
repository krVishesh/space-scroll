#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** HID physical maximum for vertical scroll resolution multiplier (Windows/Linux convention). */
#define DIAL_BLE_SCROLL_HI_RES  120

void dial_ble_init(void);
void dial_ble_start(void);
void dial_ble_send_scroll(int8_t wheel);
bool dial_ble_is_connected(void);
/** True after host (or firmware) enables the Resolution Multiplier feature report. */
bool dial_ble_hi_res_active(void);

#ifdef __cplusplus
}
#endif
