idf_component_register(
    SRCS "ota_updater.c"
    INCLUDE_DIRS "."
    REQUIRES esp_app_format esp_https_ota esp_http_client esp-tls json app_update esp_timer
    PRIV_REQUIRES esp_event nvs_flash
)
