#include "mqtt.h"
#include "zigbee.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

static void mqtt_handle_command(const char *topic, const char *data)
{
    uint16_t addr;
    if (sscanf(topic, "zigbee/%hx/set", &addr) != 1)
        return;

    zigbee_send_onoff(addr, data);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_subscribe(s_client, "zigbee/+/set", 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_DATA:
    {
        char topic[128];
        char payload[128];

        snprintf(topic, event->topic_len + 1, "%s", event->topic);
        snprintf(payload, event->data_len + 1, "%s", event->data);

        mqtt_handle_command(topic, payload);
        break;
    }

    default:
        break;
    }
}

void mqtt_start(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = "mqtt://192.168.1.10:1883"};

    s_client = esp_mqtt_client_init(&cfg);

    esp_mqtt_client_register_event(
        s_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL);

    esp_mqtt_client_start(s_client);
}

bool mqtt_publish_state(const char *topic, const char *payload)
{
    if (!s_client || !s_connected)
        return false;

    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
    return true;
}

bool mqtt_publish_device_joined(uint16_t short_addr, const uint8_t ieee_addr[8], uint8_t capability)
{
    char payload[128];

    snprintf(payload, sizeof(payload),
             "{\"event\":\"device_joined\",\"short_addr\":\"0x%04x\"}", short_addr);

    esp_mqtt_client_publish(s_client, "zigbee/device/joined", payload, 0, 0, 0);
    return true;
}

bool mqtt_publish_device_list_json(const char *json_payload)
{
    esp_mqtt_client_publish(s_client, "zigbee/device/list", json_payload, 0, 0, 0);
    return true;
}

esp_mqtt_client_handle_t mqtt_get_client(void)
{
    return s_client;
}

void mqtt_stop(void)
{
    if (s_client)
    {
        esp_mqtt_client_stop(s_client);
    }
}