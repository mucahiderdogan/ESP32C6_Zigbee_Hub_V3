#include "ha_discovery.h"
#include "mqtt.h"
#include <stdio.h>

void ha_discovery_publish_device(uint16_t short_addr,
                                 const uint8_t ieee_addr[8])
{
    char topic[128];
    char payload[256];

    snprintf(topic, sizeof(topic),
             "homeassistant/sensor/zigbee_%04x_linkquality/config",
             short_addr);

    snprintf(payload, sizeof(payload),
             "{"
             "\"name\":\"Zigbee %04x Linkquality\","
             "\"state_topic\":\"zigbee/%04x/linkquality\","
             "\"unique_id\":\"zigbee_%04x_lqi\""
             "}",
             short_addr,
             short_addr,
             short_addr);

    esp_mqtt_client_publish(mqtt_get_client(), topic, payload, 0, 1, 1);
}