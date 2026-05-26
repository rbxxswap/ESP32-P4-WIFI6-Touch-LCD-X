idf_component_register(
    SRCS "wifi_helper.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash
    PRIV_REQUIRES esp_hosted
)
