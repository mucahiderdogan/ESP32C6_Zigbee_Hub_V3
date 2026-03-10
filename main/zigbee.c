#include "zigbee.h"
#include "mqtt.h"

#include "esp_zigbee_core.h"
#include "zdo/esp_zigbee_zdo_command.h"

#include "esp_log.h"

static const char *TAG = "zigbee";

typedef struct
{
    uint16_t short_addr;
    int used;
} device_t;

static device_t devices[32];

void zigbee_init(void)
{
    ESP_LOGI(TAG, "Zigbee init");

    esp_zb_platform_config_t platform_config = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE}};

    esp_zb_platform_config(&platform_config);

    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
        .install_code_policy = false,
    };

    esp_zb_init(&zb_cfg);

    esp_zb_start(false);

    ESP_LOGI(TAG, "Zigbee coordinator started");
}

void zigbee_send_onoff(uint16_t short_addr, const char *cmd)
{
    ESP_LOGI(TAG, "Send ON/OFF to %04x : %s", short_addr, cmd);
}

void zigbee_build_device_json(char *buf, int size)
{
    int n = 0;
    n += snprintf(buf + n, size - n, "{\"devices\":[");

    int first = 1;

    for (int i = 0; i < 32; i++)
    {
        if (!devices[i].used)
            continue;

        n += snprintf(buf + n, size - n,
                      "%s{\"short_addr\":\"0x%04x\"}",
                      first ? "" : ",",
                      devices[i].short_addr);

        first = 0;
    }

    snprintf(buf + n, size - n, "]}");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_s)
{
    uint32_t *p_sg_p = signal_s->p_app_signal;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type)
    {

    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:

        ESP_LOGI(TAG, "Zigbee stack initialize ediliyor");

        esp_zb_bdb_start_top_level_commissioning(
            ESP_ZB_BDB_MODE_INITIALIZATION);

        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:

        if (signal_s->esp_err_status == ESP_OK)
        {
            ESP_LOGI(TAG, "Zigbee cihaz basladi");

            esp_zb_bdb_open_network(60);

            ESP_LOGI(TAG, "Permit join acildi: 60 sn");
        }

        break;

    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:

        ESP_LOGI(TAG, "Yeni Zigbee cihaz baglandi");

        break;

    default:
        break;
    }
}