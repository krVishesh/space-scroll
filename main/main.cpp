#include <chrono>
#include <cmath>
#include <cinttypes>
#include <memory>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "sdkconfig.h"

#include "as5600.hpp"
#include "i2c.hpp"

#include "app_log.h"
#include "board_config.h"
#include "ble/dial_ble.h"
#include "drivers/battery.h"
#include "drivers/status_led.h"

using namespace std::chrono_literals;

namespace {

constexpr espp::Logger::Verbosity kEsppLogLevel = espp::Logger::Verbosity::WARN;

/* Hi-res units per degree: DIAL_BLE_SCROLL_HI_RES units = one legacy line per BOARD_SCROLL_DEG_PER_LINE. */
constexpr float kHiResUnitsPerDegree =
    static_cast<float>(DIAL_BLE_SCROLL_HI_RES) / BOARD_SCROLL_DEG_PER_LINE;
constexpr float kScrollAccumulatorLimitHiResUnits = static_cast<float>(DIAL_BLE_SCROLL_HI_RES) * 2.0f;
constexpr float kScrollAccumulatorLimitLegacyLines = 4.0f;
constexpr int kScrollMaxHiResUnitsPerReport = 127;
constexpr int kScrollMaxCountsPerSample = 300;

std::unique_ptr<espp::As5600> g_as5600;

static int unwrap_encoder_delta(int prev_count, int count)
{
    constexpr int half_rev = espp::As5600::COUNTS_PER_REVOLUTION / 2;
    int diff = count - prev_count;
    if (diff > half_rev) {
        diff -= espp::As5600::COUNTS_PER_REVOLUTION;
    } else if (diff < -half_rev) {
        diff += espp::As5600::COUNTS_PER_REVOLUTION;
    }
    return diff;
}

void log_startup_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    APP_LOGI("Space Scroll dial starting");
    APP_LOGI("Target=%s cores=%d revision=%d", CONFIG_IDF_TARGET, chip_info.cores,
             chip_info.revision);
    APP_LOGI("I2C SCL=GPIO%d SDA=GPIO%d @ %d Hz", BOARD_I2C_SCL_GPIO, BOARD_I2C_SDA_GPIO,
             BOARD_I2C_CLK_HZ);
    APP_LOGI("WS2812=GPIO%d Battery ADC=GPIO%d", BOARD_WS2812_GPIO, BOARD_BATTERY_ADC_GPIO);
    APP_LOGI("BLE name=%s hi-res=%d units/line %.1f deg/line", BOARD_DEVICE_NAME,
             DIAL_BLE_SCROLL_HI_RES, BOARD_SCROLL_DEG_PER_LINE);
#if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    APP_LOGI("Log level=DEBUG (use sdkconfig.defaults.release for WARN-only builds)");
#else
    APP_LOGI("Log level=WARN (release)");
#endif
    APP_LOGD("Free heap=%" PRIu32 " min_heap=%" PRIu32,
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
}

bool init_encoder(void)
{
    espp::I2c i2c({
        .port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_speed = BOARD_I2C_CLK_HZ,
        .log_level = kEsppLogLevel,
    });

    std::error_code ec;
    auto as5600_device = i2c.add_device<uint8_t>(
        {.device_address = espp::As5600::DEFAULT_ADDRESS,
         .timeout_ms = static_cast<int>(i2c.config().timeout_ms),
         .scl_speed_hz = i2c.config().clk_speed,
         .log_level = kEsppLogLevel},
        ec);

    if (!as5600_device) {
        APP_LOGE("AS5600 I2C init failed: %s", ec.message().c_str());
        return false;
    }

    static constexpr float encoder_update_period =
        static_cast<float>(BOARD_ENCODER_UPDATE_MS) / 1000.0f;

    auto filter_fn = [](float raw) -> float { return raw; };

    g_as5600 = std::make_unique<espp::As5600>(espp::As5600::Config{
        .write_then_read = espp::make_i2c_addressed_write_then_read(as5600_device),
        .velocity_filter = filter_fn,
        .update_period = std::chrono::duration<float>(encoder_update_period),
        .log_level = kEsppLogLevel,
    });

    const int init_count = g_as5600->get_count();
    const float init_deg = g_as5600->get_mechanical_degrees();
    APP_LOGI("AS5600 encoder initialized count=%d pos=%.1f deg", init_count, init_deg);
    return true;
}

void update_status_led(uint8_t battery_pct, uint16_t battery_mv)
{
    (void)battery_mv;
    if (battery_mv > 0 && battery_pct <= BOARD_BATTERY_LOW_PCT) {
        status_led_set_low_battery();
        return;
    }

    if (dial_ble_is_connected()) {
        status_led_set_connected();
        return;
    }

    status_led_set_advertising();
}

static int extract_hi_res_wheel(float &scroll_accum)
{
    if (scroll_accum >= 1.0f) {
        const int wheel = static_cast<int>(std::fmin(scroll_accum, static_cast<float>(kScrollMaxHiResUnitsPerReport)));
        scroll_accum -= static_cast<float>(wheel);
        return wheel;
    }
    if (scroll_accum <= -1.0f) {
        const int wheel = static_cast<int>(std::fmax(scroll_accum, static_cast<float>(-kScrollMaxHiResUnitsPerReport)));
        scroll_accum -= static_cast<float>(wheel);
        return wheel;
    }
    return 0;
}

static int extract_legacy_wheel(float &scroll_accum)
{
    if (scroll_accum >= 1.0f) {
        scroll_accum -= 1.0f;
        return 1;
    }
    if (scroll_accum <= -1.0f) {
        scroll_accum += 1.0f;
        return -1;
    }
    return 0;
}

void dial_task(void *param)
{
    (void)param;

    TickType_t last_report = xTaskGetTickCount();
    TickType_t last_battery = xTaskGetTickCount();
    uint16_t battery_mv = 0;
    uint8_t battery_pct = 0;

    int last_count = g_as5600 ? g_as5600->get_count() : 0;
    float scroll_accum = 0.0f;

    APP_LOGI("Dial task started");

    battery_mv = battery_read_mv();
    battery_pct = battery_percent_from_mv(battery_mv);
    dial_ble_set_battery_percent(battery_pct);
    last_battery = xTaskGetTickCount();
    update_status_led(battery_pct, battery_mv);

    while (true) {
        const TickType_t now = xTaskGetTickCount();

        if ((now - last_battery) >= pdMS_TO_TICKS(BOARD_BATTERY_SAMPLE_MS)) {
            battery_mv = battery_read_mv();
            battery_pct = battery_percent_from_mv(battery_mv);
            dial_ble_set_battery_percent(battery_pct);
            last_battery = now;
            APP_LOGD("Battery %u mV (%u%%)", battery_mv, battery_pct);
        }

        if ((now - last_report) >= pdMS_TO_TICKS(BOARD_DIAL_REPORT_MS)) {
            const int count = g_as5600 ? g_as5600->get_count() : 0;
            int diff = unwrap_encoder_delta(last_count, count);

            if (diff > kScrollMaxCountsPerSample || diff < -kScrollMaxCountsPerSample) {
                APP_LOGW("Encoder spike ignored: raw=%d diff=%d", count, diff);
                last_count = count;
                diff = 0;
            } else {
                last_count = count;
            }

            const float delta_deg =
                static_cast<float>(diff) * espp::As5600::COUNTS_TO_DEGREES;

            int wheel = 0;
            if (diff != 0 && dial_ble_is_connected()) {
                if (dial_ble_hi_res_active()) {
                    scroll_accum += delta_deg * kHiResUnitsPerDegree;
                    if (scroll_accum > kScrollAccumulatorLimitHiResUnits) {
                        scroll_accum = kScrollAccumulatorLimitHiResUnits;
                    } else if (scroll_accum < -kScrollAccumulatorLimitHiResUnits) {
                        scroll_accum = -kScrollAccumulatorLimitHiResUnits;
                    }
                    wheel = extract_hi_res_wheel(scroll_accum);
                } else {
                    scroll_accum += delta_deg / BOARD_SCROLL_DEG_PER_LINE;
                    if (scroll_accum > kScrollAccumulatorLimitLegacyLines) {
                        scroll_accum = kScrollAccumulatorLimitLegacyLines;
                    } else if (scroll_accum < -kScrollAccumulatorLimitLegacyLines) {
                        scroll_accum = -kScrollAccumulatorLimitLegacyLines;
                    }
                    wheel = extract_legacy_wheel(scroll_accum);
                }
            }

            if (wheel != 0) {
                /* Clockwise dial → scroll down (Linux: negative wheel). */
                dial_ble_send_scroll(static_cast<int8_t>(-wheel));
            }

            update_status_led(battery_pct, battery_mv);

            const float pos_deg = g_as5600 ? g_as5600->get_mechanical_degrees() : 0.0f;
            APP_LOGD("Dial pos=%.2f raw=%d diff=%d delta=%.3f accum=%.2f wheel=%d hi_res=%d",
                     pos_deg, count, diff, delta_deg, scroll_accum, wheel,
                     dial_ble_hi_res_active());

            last_report = now;
        }

        vTaskDelay(pdMS_TO_TICKS(BOARD_DIAL_REPORT_MS));
    }
}

} // namespace

extern "C" void app_main(void)
{
    log_startup_info();

    status_led_init();
    battery_init();
    dial_ble_init();

    if (!init_encoder()) {
        APP_LOGE("Encoder init failed, halting");
        status_led_set_low_battery();
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    dial_ble_start();

    xTaskCreate(dial_task, "dial_task", 4096, NULL, 5, NULL);

    APP_LOGI("Space Scroll dial ready");
}
