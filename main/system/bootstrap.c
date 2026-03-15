#include "bootstrap.h"

#include "config_manager.h"
#include "device_manager.h"
#include "mqtt_bridge.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "zigbee_core.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "BOOT";

static void bootstrap_devices(void)
{
    ESP_LOGI(TAG, "Initializing device registry");

    device_manager_init();

    device_manager_add("Gateway Reset", "switch", "ctrl_reset");
    device_manager_add("Gateway Pair 60s", "switch", "ctrl_pair_60s");
}

static void bootstrap_network(void)
{
    ESP_LOGI(TAG, "Initializing WiFi");

    wifi_manager_init();

    ESP_LOGI(TAG, "Waiting for WiFi...");

    wifi_manager_wait_until_ready(portMAX_DELAY);

    if (wifi_manager_is_setup_mode()) {
        ESP_LOGI(TAG, "Factory setup network ready");
    } else {
        ESP_LOGI(TAG, "Network ready");
    }
}

static void bootstrap_zigbee(void)
{
    ESP_LOGI(TAG, "Initializing Zigbee core");
    zigbee_core_init();
}

static void bootstrap_services(void)
{
    ESP_LOGI(TAG, "Starting MQTT bridge");
    mqtt_bridge_start();

    ESP_LOGI(TAG, "Starting web server");
    web_server_start();
}

static void bootstrap_publish_initial_state(void)
{
    char json[1024];

    vTaskDelay(pdMS_TO_TICKS(2000));

    mqtt_publish_all_discovery();
    device_manager_get_json(json, sizeof(json));
    mqtt_publish_devices(json);

    ESP_LOGI(TAG, "Initial device state published");
}

void system_bootstrap_start(void)
{
    config_manager_init();
    bootstrap_devices();
    bootstrap_network();

    if (config_manager_is_factory_mode()) {
        ESP_LOGI(TAG, "Factory mode active, starting setup web server only");
        web_server_start();
        return;
    }

    bootstrap_services();
    bootstrap_zigbee();
    bootstrap_publish_initial_state();
}
