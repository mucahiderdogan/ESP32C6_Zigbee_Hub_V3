#include "wifi.h"
#include "mqtt.h"
#include "zigbee.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-C6 Zigbee Gateway starting");

    wifi_init();

    /* wifi bağlanmasını bekle */
    wifi_wait_connected();

    mqtt_start();

    zigbee_init();
}