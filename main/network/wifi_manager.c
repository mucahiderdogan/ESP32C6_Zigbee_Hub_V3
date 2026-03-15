#include "wifi_manager.h"

#include "config_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "WIFI";

EventGroupHandle_t wifi_event_group;

static bool netif_ready;
static bool setup_mode;

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting to WiFi...");
        esp_wifi_connect();
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected");
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;

        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d", event->reason);
        ESP_LOGI(TAG, "Retrying WiFi connection");
        esp_wifi_connect();
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        uint8_t mac[6];

        esp_wifi_get_mac(WIFI_IF_STA, mac);

        ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_manager_init_common(void)
{
    if (wifi_event_group == NULL) {
        wifi_event_group = xEventGroupCreate();
    }

    if (!netif_ready) {
        esp_netif_init();
        esp_event_loop_create_default();
        netif_ready = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
}

static void wifi_manager_start_setup_ap(void)
{
    uint8_t mac[6];
    char ap_ssid[32];
    wifi_config_t wifi_config = {0};

    wifi_manager_init_common();
    esp_netif_create_default_wifi_ap();

    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(ap_ssid, sizeof(ap_ssid), "ESP32-Gateway-%02X%02X", mac[4], mac[5]);

    snprintf((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", ap_ssid);
    wifi_config.ap.ssid_len = strlen(ap_ssid);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.channel = 1;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    setup_mode = true;
    ESP_LOGI(TAG, "Factory mode AP started: %s", ap_ssid);
    ESP_LOGI(TAG, "Setup page: http://192.168.4.1");
}

static void wifi_manager_start_station(void)
{
    wifi_config_t wifi_config = {0};

    wifi_manager_init_common();
    esp_netif_create_default_wifi_sta();

    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &wifi_event_handler,
                                        NULL,
                                        NULL);
    esp_event_handler_instance_register(IP_EVENT,
                                        IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler,
                                        NULL,
                                        NULL);

    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", config_manager_get_wifi_ssid());
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", config_manager_get_wifi_pass());

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    setup_mode = false;
}

void wifi_manager_init(void)
{
    if (config_manager_is_factory_mode()) {
        wifi_manager_start_setup_ap();
        return;
    }

    wifi_manager_start_station();
}

bool wifi_manager_wait_until_ready(TickType_t timeout_ticks)
{
    EventBits_t bits;

    if (setup_mode) {
        return true;
    }

    bits = xEventGroupWaitBits(wifi_event_group,
                               WIFI_CONNECTED_BIT,
                               pdFALSE,
                               pdTRUE,
                               timeout_ticks);

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_manager_is_setup_mode(void)
{
    return setup_mode;
}
