
#include "zigbee.h"
#include "esp_zigbee_core.h"
#include "esp_log.h"

static const char *TAG = "zigbee";

void zigbee_init(void)
{
    ESP_LOGI(TAG, "Zigbee Coordinator baslatiliyor...");

    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    esp_zb_platform_config(&config);

    esp_zb_init();

    esp_zb_start(false);

    ESP_LOGI(TAG, "Zigbee Coordinator baslatildi");
}
