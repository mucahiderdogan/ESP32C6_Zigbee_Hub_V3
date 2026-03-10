idf_component_register(
    SRCS
        "main.c"
        "wifi.c"
        "mqtt.c"
        "zigbee.c"
        "zigbee_device_db.c"
        "zigbee_cluster_parser.c"
        "ha_discovery.c"
        "watchdog.c"
        "ota_update.c"
        "web_dashboard.c"

    INCLUDE_DIRS "."
)