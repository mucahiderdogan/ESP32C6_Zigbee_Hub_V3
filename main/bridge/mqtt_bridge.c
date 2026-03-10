#include "mqtt_bridge.h"
#include "config.h"
#include "web_server.h"

#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t client = NULL;

/* =========================
   MQTT EVENT HANDLER
   ========================= */

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{

    switch ((esp_mqtt_event_id_t)event_id)
    {

    case MQTT_EVENT_CONNECTED:

        ESP_LOGI(TAG, "MQTT connected");

        web_log_send("MQTT connected");

        break;

    case MQTT_EVENT_DISCONNECTED:

        ESP_LOGW(TAG, "MQTT disconnected");

        web_log_send("MQTT disconnected");

        break;

    case MQTT_EVENT_ERROR:

        ESP_LOGE(TAG, "MQTT connection failed");

        web_log_send("MQTT connection failed");

        break;

    default:
        break;
    }
}

/* =========================
   START MQTT
   ========================= */

void mqtt_bridge_start(void)
{
    if (client != NULL)
    {
        ESP_LOGW(TAG, "MQTT already running");
        return;
    }

    /* MAC adresinden unique client id üret */

    uint8_t mac[6];

    esp_wifi_get_mac(WIFI_IF_STA, mac);

    char client_id[32];

    sprintf(client_id,
            "ESP32_%02X%02X%02X",
            mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "MQTT Client ID: %s", client_id);

    esp_mqtt_client_config_t mqtt_cfg = {

        .broker.address.uri = MQTT_BROKER_URI,

        .credentials.client_id = client_id,

        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS

    };

    client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(
        client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL);

    ESP_LOGI(TAG, "Connecting to MQTT broker...");

    esp_mqtt_client_start(client);
}

/* =========================
   PUBLISH DEVICES
   ========================= */

void mqtt_publish_devices(const char *json)
{
    if (!client)
        return;

    int msg_id = esp_mqtt_client_publish(
        client,
        "gateway/devices",
        json,
        0,
        1,
        0);

    ESP_LOGI(TAG, "Devices published, msg_id=%d", msg_id);
}

void mqtt_publish_ha_sensor(const char *name,
                            const char *id)
{
    char topic[128];
    char payload[512];

    sprintf(topic,
            "homeassistant/sensor/%s/config",
            id);

    sprintf(payload,
            "{"
            "\"name\":\"%s\","
            "\"state_topic\":\"gateway/device/%s/state\","
            "\"unique_id\":\"%s\","
            "\"device\":{"
            "\"name\":\"ESP32 Zigbee Gateway\","
            "\"identifiers\":[\"esp32_gateway\"]"
            "}"
            "}",
            name, id, id);

    esp_mqtt_client_publish(
        client,
        topic,
        payload,
        0,
        1,
        1);
}