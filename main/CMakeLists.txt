idf_component_register(SRCS "bt_app_av.c"
                            "bt_app_core.c"
                            "main.c"
                            "spdif.c"
                            "status_led.c"
                    PRIV_REQUIRES esp_driver_i2s bt nvs_flash esp_ringbuf esp_driver_dac esp_driver_ledc
                    INCLUDE_DIRS ".")
