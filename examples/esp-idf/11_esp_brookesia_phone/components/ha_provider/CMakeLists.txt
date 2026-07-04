idf_component_register(
    SRCS "ha_provider.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client json
    PRIV_REQUIRES ha_config
)
