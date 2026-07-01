file(GLOB_RECURSE srcs "main.cpp" "ble/*.c" "drivers/*.c")

idf_component_register(
    SRCS ${srcs}
    PRIV_REQUIRES bt esp_hid esp_event nvs_flash esp_driver_gpio esp_adc led_strip as5600 i2c
    INCLUDE_DIRS "." "ble" "drivers"
)
