#include "mqtt_bridge.h"
#include "config.h"

#include "mqtt_client.h"
#include "esp_log.h"

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t client;

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id)
    {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT connection failed");
        break;

    default:
        break;
    }
}

void mqtt_bridge_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS};

    client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(
        client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL);

    ESP_LOGI(TAG, "Connecting to MQTT broker...");
    esp_mqtt_client_start(client);
}