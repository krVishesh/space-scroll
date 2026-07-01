#include "battery.h"

#include "board_config.h"
#include "app_log.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_adc_cali_handle;
static bool s_adc_cali_enabled;

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten,
                                 adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    return calibrated;
}

void battery_init(void)
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = BOARD_BATTERY_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, BOARD_BATTERY_ADC_CHAN, &chan_config));

    s_adc_cali_enabled = adc_calibration_init(BOARD_BATTERY_ADC_UNIT, BOARD_BATTERY_ADC_CHAN,
                                                ADC_ATTEN_DB_12, &s_adc_cali_handle);
    APP_LOGI("Battery ADC initialized on GPIO %d (calibrated=%d)",
             BOARD_BATTERY_ADC_GPIO, s_adc_cali_enabled);
}

uint16_t battery_read_mv(void)
{
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, BOARD_BATTERY_ADC_CHAN, &raw));

    int adc_mv = 0;
    if (s_adc_cali_enabled) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_adc_cali_handle, raw, &adc_mv));
    } else {
        adc_mv = (raw * BOARD_BATTERY_VREF_MV) / 4095;
    }

    const float battery_mv = adc_mv * BOARD_BATTERY_DIVIDER_RATIO;
    APP_LOGD("Battery raw=%d adc_mv=%d battery_mv=%.0f", raw, adc_mv, battery_mv);

    if (battery_mv < 0.0f) {
        return 0;
    }
    if (battery_mv > 65535.0f) {
        return 65535;
    }
    return (uint16_t)battery_mv;
}

uint8_t battery_percent_from_mv(uint16_t mv)
{
    if (mv <= BOARD_BATTERY_EMPTY_MV) {
        return 0;
    }
    if (mv >= BOARD_BATTERY_FULL_MV) {
        return 100;
    }

    const float pct = ((float)(mv - BOARD_BATTERY_EMPTY_MV) * 100.0f) /
                      (float)(BOARD_BATTERY_FULL_MV - BOARD_BATTERY_EMPTY_MV);
    return (uint8_t)pct;
}

uint8_t battery_read_percent(void)
{
    return battery_percent_from_mv(battery_read_mv());
}
