#include "esp_log.h"
#include "nvs_flash.h"

#include "bootstrap.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "Gateway starting...");
    system_bootstrap_start();
}
