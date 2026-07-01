#include "status_led.h"

#include "board_config.h"
#include "app_log.h"

#include "esp_check.h"
#include "led_strip.h"

static led_strip_handle_t s_led_strip;

void status_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = BOARD_WS2812_GPIO,
        .max_leds = BOARD_WS2812_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip));
    status_led_off();
    APP_LOGI("WS2812 initialized on GPIO %d", BOARD_WS2812_GPIO);
}

void status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0, r, g, b));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_strip));
}

void status_led_off(void)
{
    ESP_ERROR_CHECK(led_strip_clear(s_led_strip));
}

void status_led_set_advertising(void)
{
    status_led_set_rgb(0, 0, 32);
}

void status_led_set_connected(void)
{
    status_led_set_rgb(0, 32, 0);
}

void status_led_set_low_battery(void)
{
    status_led_set_rgb(32, 0, 0);
}
