#include "wifi.h"
#include "mqtt.h"
#include "zigbee.h"
#include "watchdog.h"
#include "web_dashboard.h"
#include "esp_log.h"

static const char *TAG="main";

void app_main(void)
{
    ESP_LOGI(TAG,"Gateway start");

    wifi_init();
    wifi_wait_connected();

    mqtt_start();
    zigbee_init();

    watchdog_start();
    web_dashboard_start();
}