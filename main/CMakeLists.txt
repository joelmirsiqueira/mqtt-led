idf_component_register(SRCS "app_main.c" "includes/wifi.c"
                    PRIV_REQUIRES mqtt nvs_flash esp_netif esp_wifi driver
                    INCLUDE_DIRS "." "includes")
