#include "wifi.h"
#include "mqtt.h"
#include "zigbee.h"
#include "web_dashboard.h"
#include "config.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

#include <string.h>

static const char *TAG = "wifi";

static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;



/* MQTT server port test */
static bool test_server()
{
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = inet_addr("192.168.61.114");
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(1883);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

    if (sock < 0)
    {
        ESP_LOGE(TAG, "Socket olusturulamadi");
        return false;
    }

    ESP_LOGI(TAG, "Server test: 192.168.61.114:1883");

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    close(sock);

    if (err != 0)
    {
        ESP_LOGW(TAG, "Server ulasilamadi");
        return false;
    }

    return true;
}



/* Network servislerini yöneten task */
static void network_ready_task(void *arg)
{
    ESP_LOGI(TAG, "Server test baslatiliyor");

    bool server_ok = false;

    for (int i = 1; i <= 5; i++)
    {
        ESP_LOGI(TAG, "Deneme %d/5", i);

        if (test_server())
        {
            ESP_LOGI(TAG, "Server ulasilabilir");
            server_ok = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (!server_ok)
    {
        ESP_LOGE(TAG, "Server ulasilamiyor");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Network hazir");

    /* Zigbee başlat */
    zigbee_init();

    vTaskDelay(pdMS_TO_TICKS(2000));

    /* MQTT başlat */
    mqtt_start();

    /* Web dashboard */
    web_dashboard_start();

    /* MQTT bağlantısını izle */
    while (1)
    {
        if (!mqtt_is_running())
        {
            ESP_LOGW(TAG, "MQTT yeniden baglaniyor");
            mqtt_start();
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}



/* WiFi event handler */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WiFi STA start");
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi disconnected");

        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

        mqtt_stop();

        esp_wifi_connect();
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "WiFi IP alindi: " IPSTR, IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);

        xTaskCreate(
            network_ready_task,
            "network_ready",
            4096,
            NULL,
            5,
            NULL);
    }
}



/* WiFi init */
void wifi_init(void)
{

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &wifi_event_handler,
                                               NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler,
                                               NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialized");
}



/* bağlantı kontrol */
bool wifi_is_connected(void)
{
    if (!wifi_event_group)
        return false;

    EventBits_t bits = xEventGroupGetBits(wifi_event_group);

    return (bits & WIFI_CONNECTED_BIT) != 0;
}