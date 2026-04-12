idf_component_register(SRCS 
                     "esp32ble.c"
PRIV_REQUIRES bt nvs_flash esp_http_client esp_driver_gpio
                    INCLUDE_DIRS ".")
