idf_component_register(
    SRCS "ble_pad.c"
    INCLUDE_DIRS "include"
    REQUIRES bt esp_hid nvs_flash esp_event esp_timer
)
