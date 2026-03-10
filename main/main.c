
#include "core/event_bus.h"
#include "config/config_manager.h"
#include "network/wifi_manager.h"
#include "zigbee/zigbee_core.h"
#include "device/device_manager.h"
#include "bridge/mqtt_bridge.h"
#include "services/web_server.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "gateway";

void app_main(void)
{
    nvs_flash_init();

    config_manager_init();
    event_bus_init();
    wifi_manager_init();

    device_manager_init();
    zigbee_core_init();

    mqtt_bridge_start();
    web_server_start();
}
