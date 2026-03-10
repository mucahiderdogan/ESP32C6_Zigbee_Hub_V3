#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "mqtt_bridge.h"
#include "web_server.h"

static const char *TAG = "SYSTEM";

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "Gateway starting...");

    wifi_manager_init();

    ESP_LOGI(TAG, "Waiting for WiFi...");

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );

    ESP_LOGI(TAG, "Network ready");

    mqtt_bridge_start();

    web_server_start();
}