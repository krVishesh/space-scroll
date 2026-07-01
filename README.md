# Space Scroll

High-resolution wireless scroll dial for ESP32-C6 using an AS5600 magnetic encoder, BLE HID Resolution Multiplier (NimBLE), WS2812 status LED, and LiPo battery monitoring.

## Hardware

| Function | GPIO |
| -------- | ---- |
| AS5600 SCL | 19 |
| AS5600 SDA | 20 |
| WS2812 data | 8 |
| Battery divider | 5 (R1=100K to Li+, R2=220K to GND) |

## Dependencies

Managed via `main/idf_component.yml`:

- `espp/as5600^1.1.4` — magnetic encoder driver
- `espp/i2c^1.1.4` — I2C helper used by the AS5600 example API
- `espressif/led_strip^3.0.0` — WS2812 driver

Install or refresh dependencies:

```bash
idf.py add-dependency "espp/as5600^1.1.4"
idf.py add-dependency "espp/i2c^1.1.4"
idf.py add-dependency "espressif/led_strip^3.0.0"
```

## Build

Development build (verbose logging):

```bash
idf.py set-target esp32c6
idf.py build flash monitor
```

Release build (reduced logging):

```bash
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.release" build
```

## BLE HID hi-res scroll dial

Advertises as **SpaceScroll** (BLE HID mouse with Resolution Multiplier). Dial rotation maps AS5600 angle changes to fine-grained scroll wheel units; Linux/Windows negotiate hi-res via the HID feature report (same model as [Engineer Bo’s Full Scroll Dial](https://www.youtube.com/watch?v=FSy9G6bNuKA)).

Sensitivity in `main/board_config.h`:

- `BOARD_SCROLL_DEG_PER_LINE` — dial degrees per one full legacy scroll line (higher = slower; default `12.0` → 10 hi-res units per degree, 120 units per line)
- `BOARD_HID_HI_RES_ENABLE_MS` — delay after connect before enabling the multiplier feature report (default `500`)

The project includes a patched local `components/esp_hid` so NimBLE registers mouse **feature** reports (required for host hi-res negotiation; stock ESP-IDF drops them). **After changing `components/esp_hid`, run `idf.py fullclean build`** so CMake picks up the override instead of the IDF copy.

**After flashing, remove and re-pair** so the host loads the new report map and product ID:

```bash
bluetoothctl
# remove XX:XX:XX:XX:XX:XX
# scan on
# pair/trust/connect SpaceScroll
```

### Verify on Linux

```bash
sudo evtest
# pick SpaceScroll, rotate dial slowly
```

**Hi-res working:** `REL_WHEEL_HI_RES` shows small varying values (±1, ±2, ±5…). `REL_WHEEL` is 0 most of the time (±1 only when accumulated hi-res crosses 120).

**Broken (multiplier not negotiated):** every event has `REL_WHEEL=±1` and `REL_WHEEL_HI_RES=±120` together — re-pair and check serial log for `HID hi-res scroll enabled`.

Optional udev hwdb if scroll feels wrong on some setups ([ESP32-BLE-Mouse #78](https://github.com/T-vK/ESP32-BLE-Mouse/issues/78)):

```bash
# /etc/udev/hwdb.d/11-spacescroll-mouse.hwdb
mouse:*:name:SpaceScroll:
 MOUSE_WHEEL_CLICK_ANGLE=7.5
 MOUSE_WHEEL_CLICK_COUNT=120
```

Then `sudo systemd-hwdb update && sudo udevadm trigger`.

Confirm descriptor includes Resolution Multiplier (`0x48`):

```bash
for d in /sys/bus/hid/devices/*; do
  grep -q SpaceScroll "$d/uevent" 2>/dev/null || continue
  xxd "$d/report_descriptor"
done
```

## Logging

- **Debug/dev** (`sdkconfig.defaults`): `LOG_DEFAULT_LEVEL_DEBUG` — startup details and `ESP_LOGD` dial samples
- **Release** (`sdkconfig.defaults.release`): `LOG_DEFAULT_LEVEL_WARN` — errors and warnings only

Battery divider values are defined in `main/board_config.h` (R1=100K, R2=220K → ratio ≈1.45).
