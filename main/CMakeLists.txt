idf_component_register(
    SRCS "main.c" "medal.c"
    INCLUDE_DIRS "."
    REQUIRES display doom ble_pad nvs_flash driver esp_timer esp_partition esp_adc
)

# doom1.whd (whd_gen -no-super-tiny from the shareware doom1.wad) goes in the 'wad' partition.
set(WHD "${CMAKE_SOURCE_DIR}/build-artifacts/doom1.whd")
esptool_py_flash_to_partition(flash "wad" "${WHD}")
