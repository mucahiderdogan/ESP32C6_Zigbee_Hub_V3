#include "watchdog.h"
#include "mqtt.h"
#include "wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG="watchdog";

static void watchdog_task(void *arg)
{
    while(1)
    {
        if(!wifi_is_connected())
            ESP_LOGW(TAG,"wifi disconnected");

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void watchdog_start()
{
    xTaskCreate(watchdog_task,
                "watchdog",
                4096,
                0,
                5,
                0);
}