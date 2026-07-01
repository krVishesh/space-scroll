#include "dial_ble.h"

#include "app_log.h"
#include "board_config.h"
#include "esp_hid_common.h"
#include "esp_hid_gap.h"

#include "esp_event.h"
#include "esp_hidd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define MOUSE_REPORT_ID  1

static esp_hidd_dev_t *s_hid_dev;
static bool s_ble_connected;
static volatile bool s_hi_res_active;
static bool s_has_feature_report;

/* Hi-res BLE mouse with Resolution Multiplier (usage 0x48) feature report.
 * Layout matches ESP32-BLE-Mouse #78 / Engineer Bo / Microsoft wheel.docx pattern.
 *
 * Input report ID 1: [X, Y, Wheel] — wheel carries hi-res units when multiplier active.
 * Feature report ID 1: multiplier logical 0=1x legacy, 1=120x hi-res. */
static const uint8_t s_mouse_report_map[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x02,       /* Usage (Mouse) */
    0xA1, 0x01,       /* Collection (Application) */
    0x05, 0x01,       /*   Usage Page (Generic Desktop) */
    0x09, 0x02,       /*   Usage (Mouse) */
    0xA1, 0x02,       /*   Collection (Logical) */
    0x85, 0x01,       /*     Report ID (1) */
    0x09, 0x01,       /*     Usage (Pointer) */
    0xA1, 0x00,       /*     Collection (Physical) */
    0x05, 0x01,       /*       Usage Page (Generic Desktop) */
    0x09, 0x30,       /*       Usage (X) */
    0x09, 0x31,       /*       Usage (Y) */
    0x15, 0x81,       /*       Logical Minimum (-127) */
    0x25, 0x7F,       /*       Logical Maximum (127) */
    0x75, 0x08,       /*       Report Size (8) */
    0x95, 0x02,       /*       Report Count (2) */
    0x81, 0x06,       /*       Input (Data,Var,Rel) */
    0x09, 0x38,       /*       Usage (Wheel) */
    0x35, 0x00,       /*       Physical Minimum (0) */
    0x46, 0x00, 0x00, /*       Physical Maximum (0) */
    0x15, 0x81,       /*       Logical Minimum (-127) */
    0x25, 0x7F,       /*       Logical Maximum (127) */
    0x75, 0x08,       /*       Report Size (8) */
    0x95, 0x01,       /*       Report Count (1) */
    0x81, 0x06,       /*       Input (Data,Var,Rel) */
    0x05, 0x01,       /*       Usage Page (Generic Desktop) */
    0x09, 0x48,       /*       Usage (Resolution Multiplier) */
    0x15, 0x00,       /*       Logical Minimum (0) */
    0x25, 0x01,       /*       Logical Maximum (1) */
    0x35, 0x01,       /*       Physical Minimum (1) */
    0x46, 0x78, 0x00, /*       Physical Maximum (120) */
    0x75, 0x08,       /*       Report Size (8) */
    0x95, 0x01,       /*       Report Count (1) */
    0xB1, 0x02,       /*       Feature (Data,Var,Abs) */
    0xC0,             /*     End Collection (Physical) */
    0xC0,             /*   End Collection (Logical) */
    0xC0,             /* End Collection (Application) */
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    {
        .data = s_mouse_report_map,
        .len = sizeof(s_mouse_report_map),
    },
};

static esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05E0,
    .version = 0x0200,
    .device_name = BOARD_DEVICE_NAME,
    .manufacturer_name = "SpaceScroll",
    .serial_number = "000001",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

void ble_hid_task_start_up(void)
{
    /* Required by esp_hid_gap.c on encryption; no stdin demo task needed. */
}

void ble_store_config_init(void);

static void log_hid_report_map(void)
{
    esp_hid_report_map_t *map =
        esp_hid_parse_report_map(s_mouse_report_map, sizeof(s_mouse_report_map));
    if (map == NULL) {
        APP_LOGE("HID report map parse failed");
        return;
    }

    APP_LOGI("HID report map: %u reports", map->reports_len);
    for (unsigned i = 0; i < map->reports_len; i++) {
        const esp_hid_report_item_t *r = &map->reports[i];
        APP_LOGI("  [%u] id=%u type=%s len=%u", i, r->report_id,
                 esp_hid_report_type_str(r->report_type), r->value_len);
        if (r->report_type == ESP_HID_REPORT_TYPE_FEATURE && r->report_id == MOUSE_REPORT_ID) {
            s_has_feature_report = true;
        }
    }
    esp_hid_free_report_map(map);

    if (!s_has_feature_report) {
        APP_LOGE("HID feature report missing — run: idf.py fullclean build");
    }
}

static void hi_res_enable_task(void *param)
{
    (void)param;
    vTaskDelay(pdMS_TO_TICKS(BOARD_HID_HI_RES_ENABLE_MS));

    if (s_hid_dev == NULL || !esp_hidd_dev_connected(s_hid_dev)) {
        vTaskDelete(NULL);
        return;
    }

    if (!s_has_feature_report) {
        APP_LOGW("HID hi-res unavailable (no feature report); using legacy scroll");
        vTaskDelete(NULL);
        return;
    }

    /* Linux SET_REPORTs the multiplier on probe; NimBLE stores it in the feature
     * characteristic. Avoid esp_hidd_dev_feature_set() — it needs the feature GATT char. */
    s_hi_res_active = true;
    APP_LOGI("HID hi-res scroll active (multiplier=%d)", DIAL_BLE_SCROLL_HI_RES);

    vTaskDelete(NULL);
}

static void hid_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch ((esp_hidd_event_t)id) {
    case ESP_HIDD_START_EVENT:
        APP_LOGI("HID stack started, advertising");
        esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        APP_LOGI("HID host connected");
        s_ble_connected = true;
        s_hi_res_active = false;
        xTaskCreate(hi_res_enable_task, "hi_res_en", 2048, NULL, 5, NULL);
        break;
    case ESP_HIDD_FEATURE_EVENT:
        if (param != NULL && param->feature.length > 0 && param->feature.data != NULL) {
            const bool enabled = param->feature.data[0] != 0;
            s_hi_res_active = enabled;
            APP_LOGI("HID feature report id=%u len=%u val=0x%02x hi-res=%s",
                     param->feature.report_id, param->feature.length, param->feature.data[0],
                     enabled ? "on" : "off");
        }
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        APP_LOGI("HID host disconnected");
        s_ble_connected = false;
        s_hi_res_active = false;
        esp_hid_ble_gap_adv_start();
        break;
    default:
        break;
    }
}

static void nimble_host_task(void *param)
{
    (void)param;
    APP_LOGI("NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void dial_ble_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_hid_gap_init(HIDD_BLE_MODE));

    int name_rc = ble_svc_gap_device_name_set(BOARD_DEVICE_NAME);
    if (name_rc != 0) {
        APP_LOGE("GAP device name set failed: %d", name_rc);
    }

    ESP_ERROR_CHECK(esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_MOUSE, s_hid_config.device_name));

    s_has_feature_report = false;
    log_hid_report_map();

    ESP_ERROR_CHECK(esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE, hid_event_handler, &s_hid_dev));

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    APP_LOGI("BLE HID hi-res scroll dial initialized");
}

void dial_ble_start(void)
{
    esp_err_t ret = esp_nimble_enable(nimble_host_task);
    if (ret != ESP_OK) {
        APP_LOGE("esp_nimble_enable failed: %d", ret);
    }
}

void dial_ble_send_scroll(int8_t wheel)
{
    if (s_hid_dev == NULL || !esp_hidd_dev_connected(s_hid_dev) || wheel == 0) {
        return;
    }

    int8_t report[3] = {0, 0, wheel};
    esp_err_t err = esp_hidd_dev_input_set(s_hid_dev, 0, MOUSE_REPORT_ID, (uint8_t *)report, sizeof(report));
    if (err != ESP_OK) {
        APP_LOGD("HID scroll report failed: %d", err);
    } else {
        APP_LOGD("HID scroll wheel=%d hi-res=%d", wheel, s_hi_res_active);
    }
}

bool dial_ble_is_connected(void)
{
    return s_ble_connected;
}

bool dial_ble_hi_res_active(void)
{
    return s_hi_res_active;
}
