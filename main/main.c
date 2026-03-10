#include "wifi.h"
#include "web_dashboard.h"

void app_main(void)
{
    wifi_init();

    web_dashboard_start();
}