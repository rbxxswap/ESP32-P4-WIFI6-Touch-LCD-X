idf_component_register(
    SRCS "ha_provider.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt
    PRIV_REQUIRES ha_config
)
