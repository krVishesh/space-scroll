#pragma once

#include "esp_log.h"

#define APP_TAG "space_scroll"

#if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
#define APP_LOGD(fmt, ...) ESP_LOGD(APP_TAG, fmt, ##__VA_ARGS__)
#else
#define APP_LOGD(fmt, ...) ((void)0)
#endif

#define APP_LOGI(fmt, ...) ESP_LOGI(APP_TAG, fmt, ##__VA_ARGS__)
#define APP_LOGW(fmt, ...) ESP_LOGW(APP_TAG, fmt, ##__VA_ARGS__)
#define APP_LOGE(fmt, ...) ESP_LOGE(APP_TAG, fmt, ##__VA_ARGS__)
