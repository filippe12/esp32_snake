idf_component_register(SRCS "snake.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_i2c u8g2 u8g2-hal-esp-idf)
