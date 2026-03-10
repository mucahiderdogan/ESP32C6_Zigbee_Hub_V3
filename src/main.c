
#include "wifi.h"
#include "mqtt.h"
#include "zigbee.h"
#include "config.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "WiFi hedef SSID: %s", WIFI_SSID);
    wifi_init();
    ESP_LOGI(TAG, "MQTT hedef URI: %s", MQTT_URI);
    mqtt_start();
    zigbee_init();
}
