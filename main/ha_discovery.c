#include "ha_discovery.h"
#include "mqtt.h"
#include "mqtt_client.h"
#include <stdio.h>

static void publish_config(const char *topic, const char *payload)
{
    esp_mqtt_client_handle_t client = mqtt_get_client();
    if (!client) return;

    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);
}

static void build_device(char *buf, size_t size, uint16_t addr)
{
    snprintf(buf, size,
        "\"device\":{"
        "\"identifiers\":[\"zigbee_%04x\"],"
        "\"name\":\"Zigbee Device %04x\","
        "\"manufacturer\":\"ESP32 Zigbee Hub\","
        "\"model\":\"Generic Zigbee Device\""
        "}",
        addr, addr);
}

void ha_discovery_publish_linkquality(uint16_t addr)
{
    char topic[128];
    char payload[512];
    char device[256];

    build_device(device, sizeof(device), addr);

    snprintf(topic, sizeof(topic),
             "homeassistant/sensor/zigbee_%04x_linkquality/config", addr);

    snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"Zigbee %04x Linkquality\","
        "\"state_topic\":\"zigbee/%04x/linkquality\","
        "\"unique_id\":\"zigbee_%04x_lqi\","
        "%s"
        "}",
        addr, addr, addr, device);

    publish_config(topic, payload);
}

void ha_discovery_publish_temperature(uint16_t addr)
{
    char topic[128];
    char payload[512];
    char device[256];

    build_device(device, sizeof(device), addr);

    snprintf(topic, sizeof(topic),
             "homeassistant/sensor/zigbee_%04x_temperature/config", addr);

    snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"Zigbee %04x Temperature\","
        "\"state_topic\":\"zigbee/%04x/temperature\","
        "\"unit_of_measurement\":\"°C\","
        "\"device_class\":\"temperature\","
        "\"unique_id\":\"zigbee_%04x_temp\","
        "%s"
        "}",
        addr, addr, addr, device);

    publish_config(topic, payload);
}

void ha_discovery_publish_humidity(uint16_t addr)
{
    char topic[128];
    char payload[512];
    char device[256];

    build_device(device, sizeof(device), addr);

    snprintf(topic, sizeof(topic),
             "homeassistant/sensor/zigbee_%04x_humidity/config", addr);

    snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"Zigbee %04x Humidity\","
        "\"state_topic\":\"zigbee/%04x/humidity\","
        "\"unit_of_measurement\":\"%%\","
        "\"device_class\":\"humidity\","
        "\"unique_id\":\"zigbee_%04x_hum\","
        "%s"
        "}",
        addr, addr, addr, device);

    publish_config(topic, payload);
}

void ha_discovery_publish_battery(uint16_t addr)
{
    char topic[128];
    char payload[512];
    char device[256];

    build_device(device, sizeof(device), addr);

    snprintf(topic, sizeof(topic),
             "homeassistant/sensor/zigbee_%04x_battery/config", addr);

    snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"Zigbee %04x Battery\","
        "\"state_topic\":\"zigbee/%04x/battery\","
        "\"unit_of_measurement\":\"%%\","
        "\"device_class\":\"battery\","
        "\"unique_id\":\"zigbee_%04x_bat\","
        "%s"
        "}",
        addr, addr, addr, device);

    publish_config(topic, payload);
}

void ha_discovery_publish_contact(uint16_t addr)
{
    char topic[128];
    char payload[512];
    char device[256];

    build_device(device, sizeof(device), addr);

    snprintf(topic, sizeof(topic),
             "homeassistant/binary_sensor/zigbee_%04x_contact/config", addr);

    snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"Zigbee %04x Contact\","
        "\"state_topic\":\"zigbee/%04x/contact\","
        "\"device_class\":\"door\","
        "\"payload_on\":\"OPEN\","
        "\"payload_off\":\"CLOSED\","
        "\"unique_id\":\"zigbee_%04x_contact\","
        "%s"
        "}",
        addr, addr, addr, device);

    publish_config(topic, payload);
}

void ha_discovery_publish_motion(uint16_t addr)
{
    char topic[128];
    char payload[512];
    char device[256];

    build_device(device, sizeof(device), addr);

    snprintf(topic, sizeof(topic),
             "homeassistant/binary_sensor/zigbee_%04x_motion/config", addr);

    snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"Zigbee %04x Motion\","
        "\"state_topic\":\"zigbee/%04x/motion\","
        "\"device_class\":\"motion\","
        "\"payload_on\":\"ON\","
        "\"payload_off\":\"OFF\","
        "\"unique_id\":\"zigbee_%04x_motion\","
        "%s"
        "}",
        addr, addr, addr, device);

    publish_config(topic, payload);
}