#include "mqtt_bridge.h"

#include "config.h"
#include "device_manager.h"
#include "web_server.h"
#include "zigbee_core.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

#include <stdio.h>
#include <string.h>

#define GATEWAY_DEVICE_ID       "esp32_gateway"
#define RESET_SWITCH_ID         "gateway_reset"
#define PAIR_SWITCH_ID          "gateway_pair_60s"
#define RESET_COMMAND_TOPIC     "gateway/control/reset/set"
#define RESET_STATE_TOPIC       "gateway/control/reset/state"
#define PAIR_COMMAND_TOPIC      "gateway/control/pair_60s/set"
#define PAIR_STATE_TOPIC        "gateway/control/pair_60s/state"

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t client;

static void mqtt_publish_ha_switch(const char *name,
                                   const char *unique_id,
                                   const char *command_topic,
                                   const char *state_topic,
                                   const char *icon)
{
    char topic[128];
    char payload[768];

    snprintf(topic, sizeof(topic), "homeassistant/switch/%s/config", unique_id);
    snprintf(payload,
             sizeof(payload),
             "{"
             "\"name\":\"%s\","
             "\"unique_id\":\"%s\","
             "\"command_topic\":\"%s\","
             "\"state_topic\":\"%s\","
             "\"payload_on\":\"ON\","
             "\"payload_off\":\"OFF\","
             "\"icon\":\"%s\","
             "\"device\":{"
             "\"name\":\"ESP32 Zigbee Gateway\","
             "\"identifiers\":[\"%s\"]"
             "}"
             "}",
             name,
             unique_id,
             command_topic,
             state_topic,
             icon,
             GATEWAY_DEVICE_ID);

    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);
}

static void mqtt_publish_ha_joined_sensor(const char *name, const char *ieee)
{
    char topic[160];
    char payload[768];

    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", ieee);
    snprintf(payload,
             sizeof(payload),
             "{"
             "\"name\":\"%s\","
             "\"unique_id\":\"zigbee_%s\","
             "\"state_topic\":\"gateway/device/%s/state\","
             "\"json_attributes_topic\":\"gateway/device/%s/attributes\","
             "\"entity_category\":\"diagnostic\","
             "\"icon\":\"mdi:zigbee\","
             "\"device\":{"
             "\"name\":\"%s\","
             "\"identifiers\":[\"zigbee_%s\"]"
             "}"
             "}",
             name,
             ieee,
             ieee,
             ieee,
             name,
             ieee);

    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);
}

static void mqtt_publish_text_state(const char *topic, const char *payload, bool retained)
{
    if (client == NULL) {
        return;
    }

    esp_mqtt_client_publish(client, topic, payload, 0, 1, retained ? 1 : 0);
}

static void mqtt_handle_command(const char *topic, const char *payload)
{
    if (strcmp(topic, PAIR_COMMAND_TOPIC) == 0) {
        if (strcmp(payload, "ON") == 0 && zigbee_core_start_pairing()) {
            mqtt_publish_pair_state(true);
        } else {
            mqtt_publish_pair_state(zigbee_core_is_pairing_active());
        }
        return;
    }

    if (strcmp(topic, RESET_COMMAND_TOPIC) == 0) {
        if (strcmp(payload, "ON") == 0) {
            mqtt_publish_reset_state(false);
            web_log_send("Gateway restarting");
            esp_restart();
        } else {
            mqtt_publish_reset_state(false);
        }
    }
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        web_log_send("MQTT connected");
        esp_mqtt_client_subscribe(client, PAIR_COMMAND_TOPIC, 1);
        esp_mqtt_client_subscribe(client, RESET_COMMAND_TOPIC, 1);
        mqtt_publish_all_discovery();
        mqtt_publish_pair_state(zigbee_core_is_pairing_active());
        mqtt_publish_reset_state(false);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        web_log_send("MQTT disconnected");
        break;

    case MQTT_EVENT_DATA: {
        char topic[128];
        char payload[32];

        snprintf(topic, sizeof(topic), "%.*s", event->topic_len, event->topic);
        snprintf(payload, sizeof(payload), "%.*s", event->data_len, event->data);

        ESP_LOGI(TAG, "MQTT command %s => %s", topic, payload);
        mqtt_handle_command(topic, payload);
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT connection failed");
        web_log_send("MQTT connection failed");
        break;

    default:
        break;
    }
}

void mqtt_bridge_start(void)
{
    if (client != NULL) {
        ESP_LOGW(TAG, "MQTT already running");
        return;
    }

    uint8_t mac[6];
    char client_id[32];

    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(client_id, sizeof(client_id), "ESP32_%02X%02X%02X", mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "MQTT Client ID: %s", client_id);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = client_id,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    ESP_LOGI(TAG, "Connecting to MQTT broker...");
    esp_mqtt_client_start(client);
}

void mqtt_publish_devices(const char *json)
{
    if (client == NULL) {
        return;
    }

    int msg_id = esp_mqtt_client_publish(client, "gateway/devices", json, 0, 1, 1);
    ESP_LOGI(TAG, "Devices published, msg_id=%d", msg_id);
}

void mqtt_publish_all_discovery(void)
{
    if (client == NULL) {
        return;
    }

    mqtt_publish_ha_switch("Gateway Reset",
                           RESET_SWITCH_ID,
                           RESET_COMMAND_TOPIC,
                           RESET_STATE_TOPIC,
                           "mdi:restart");
    mqtt_publish_ha_switch("Gateway Pair 60s",
                           PAIR_SWITCH_ID,
                           PAIR_COMMAND_TOPIC,
                           PAIR_STATE_TOPIC,
                           "mdi:zigbee");

    for (int i = 0; i < device_manager_count(); i++) {
        device_t *device = device_manager_get(i);

        if (device == NULL || device_manager_is_control(device)) {
            continue;
        }

        mqtt_publish_ha_joined_sensor(device->name, device->ieee);
        mqtt_publish_joined_device(device->name, device->ieee);
    }
}

void mqtt_publish_joined_device(const char *name, const char *ieee)
{
    device_t *device = NULL;
    char topic[128];
    char payload[512];
    int index;

    if (client == NULL) {
        return;
    }

    index = device_manager_find_by_ieee(ieee);
    if (index >= 0) {
        device = device_manager_get(index);
    }

    mqtt_publish_ha_joined_sensor(name, ieee);

    snprintf(topic, sizeof(topic), "gateway/device/%s/state", ieee);
    mqtt_publish_text_state(topic, (device && device->type[0] != '\0') ? device->type : "joined", true);

    if (device != NULL) {
        snprintf(topic, sizeof(topic), "gateway/device/%s/attributes", ieee);
        snprintf(payload,
                 sizeof(payload),
                 "{"
                 "\"name\":\"%s\","
                 "\"type\":\"%s\","
                 "\"manufacturer\":\"%s\","
                 "\"model\":\"%s\","
                 "\"features\":\"%s\","
                 "\"short_addr\":%u,"
                 "\"endpoint\":%u,"
                 "\"profile_id\":%u,"
                 "\"device_id\":%u"
                 "}",
                 device->name,
                 device->type,
                 device->manufacturer,
                 device->model,
                 device->features,
                 device->short_addr,
                 device->endpoint,
                 device->profile_id,
                 device->device_id);
        mqtt_publish_text_state(topic, payload, true);
    }
}

void mqtt_publish_pair_state(bool active)
{
    mqtt_publish_text_state(PAIR_STATE_TOPIC, active ? "ON" : "OFF", true);
}

void mqtt_publish_reset_state(bool active)
{
    mqtt_publish_text_state(RESET_STATE_TOPIC, active ? "ON" : "OFF", true);
}
