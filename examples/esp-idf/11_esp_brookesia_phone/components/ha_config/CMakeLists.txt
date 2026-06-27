idf_component_register(
    SRCS "ha_config.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server
    PRIV_REQUIRES nvs_flash esp_netif
)
