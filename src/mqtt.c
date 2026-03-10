
#include "mqtt.h"
#include "config.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "mqtt";

static void mqtt_extract_host(const char *uri, char *host, size_t host_size)
{
    const char *start = strstr(uri, "://");
    const char *p = start ? (start + 3) : uri;
    const char *end = p;
    size_t len;

    while (*end && *end != ':' && *end != '/') {
        end++;
    }

    len = (size_t)(end - p);
    if (len >= host_size) {
        len = host_size - 1;
    }

    memcpy(host, p, len);
    host[len] = '\0';
}

void mqtt_start(void)
{
    char mqtt_host[64];

    mqtt_extract_host(MQTT_URI, mqtt_host, sizeof(mqtt_host));
    ESP_LOGI(TAG, "MQTT hedefi: URI=%s, host/IP=%s", MQTT_URI, mqtt_host);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URI,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);

    ESP_LOGI(TAG, "MQTT baslatildi");
}
