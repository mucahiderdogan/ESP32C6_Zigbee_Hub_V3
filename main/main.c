#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "mqtt_bridge.h"
#include "web_server.h"
#include "device_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "SYSTEM";

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "Gateway starting...");

    /* =========================
       Device Manager
       ========================= */

    device_manager_init();

    /* test cihazları */

    device_manager_add("Temp Sensor", "sensor", "0x00124b0001");
    device_manager_add("Smart Bulb", "light", "0x00124b0002");

    /* =========================
       WiFi
       ========================= */

    wifi_manager_init();

    ESP_LOGI(TAG, "Waiting for WiFi...");

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    ESP_LOGI(TAG, "Network ready");

    /* =========================
       MQTT
       ========================= */

    mqtt_bridge_start();

    /* =========================
       WEB SERVER
       ========================= */

    web_server_start();

    mqtt_publish_ha_sensor("Temp Sensor", "temp_sensor");
    mqtt_publish_ha_sensor("Smart Bulb", "smart_bulb");

    /* =========================
       MQTT'ye device listesi gönder
       ========================= */

    vTaskDelay(pdMS_TO_TICKS(2000));

    char json[1024];

    device_manager_get_json(json, sizeof(json));

    mqtt_publish_devices(json);

    ESP_LOGI(TAG, "Device list published to MQTT");
}