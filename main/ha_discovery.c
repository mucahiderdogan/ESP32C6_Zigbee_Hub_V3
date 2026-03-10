#include "ha_discovery.h"
#include "mqtt.h"
#include <stdio.h>

void ha_discovery_publish(uint16_t short_addr)
{
    char topic[128];
    char payload[256];

    sprintf(topic,
    "homeassistant/switch/zigbee_%04x/config",
    short_addr);

    sprintf(payload,
    "{"
    "\"name\":\"Zigbee %04x\","
    "\"state_topic\":\"zigbee/device/%04x/state\","
    "\"command_topic\":\"zigbee/device/%04x/set\","
    "\"unique_id\":\"zigbee_%04x\""
    "}",
    short_addr,
    short_addr,
    short_addr,
    short_addr);

    mqtt_publish(topic,payload);
}