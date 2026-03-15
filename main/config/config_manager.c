#include "config_manager.h"

#include "nvs.h"
#include "esp_log.h"

#include <string.h>

#define CONFIG_NAMESPACE "gateway_cfg"
#define KEY_WIFI_SSID    "wifi_ssid"
#define KEY_WIFI_PASS    "wifi_pass"
#define KEY_MQTT_URI     "mqtt_uri"
#define KEY_MQTT_USER    "mqtt_user"
#define KEY_MQTT_PASS    "mqtt_pass"

static const char *TAG = "CONFIG";

static gateway_config_t current_config;
static bool config_loaded;
static bool factory_mode = true;

static esp_err_t config_read_string(nvs_handle_t handle,
                                    const char *key,
                                    char *dest,
                                    size_t dest_size)
{
    size_t required = dest_size;
    esp_err_t err;

    if (dest == NULL || dest_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    dest[0] = '\0';
    err = nvs_get_str(handle, key, dest, &required);
    if (err != ESP_OK) {
        dest[0] = '\0';
    }

    return err;
}

void config_manager_init(void)
{
    nvs_handle_t handle;

    memset(&current_config, 0, sizeof(current_config));
    config_loaded = false;
    factory_mode = true;

    if (nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Config namespace unavailable, starting in factory mode");
        return;
    }

    config_read_string(handle, KEY_WIFI_SSID, current_config.wifi_ssid, sizeof(current_config.wifi_ssid));
    config_read_string(handle, KEY_WIFI_PASS, current_config.wifi_pass, sizeof(current_config.wifi_pass));
    config_read_string(handle, KEY_MQTT_URI, current_config.mqtt_broker_uri, sizeof(current_config.mqtt_broker_uri));
    config_read_string(handle, KEY_MQTT_USER, current_config.mqtt_user, sizeof(current_config.mqtt_user));
    config_read_string(handle, KEY_MQTT_PASS, current_config.mqtt_pass, sizeof(current_config.mqtt_pass));
    nvs_close(handle);

    config_loaded = true;
    factory_mode = current_config.wifi_ssid[0] == '\0' ||
                   current_config.mqtt_broker_uri[0] == '\0';

    ESP_LOGI(TAG, "Config mode: %s", factory_mode ? "factory" : "configured");
}

bool config_manager_is_factory_mode(void)
{
    return factory_mode;
}

bool config_manager_get(gateway_config_t *config)
{
    if (!config_loaded || config == NULL) {
        return false;
    }

    *config = current_config;
    return true;
}

const char *config_manager_get_wifi_ssid(void)
{
    return current_config.wifi_ssid;
}

const char *config_manager_get_wifi_pass(void)
{
    return current_config.wifi_pass;
}

const char *config_manager_get_mqtt_broker_uri(void)
{
    return current_config.mqtt_broker_uri;
}

const char *config_manager_get_mqtt_user(void)
{
    return current_config.mqtt_user;
}

const char *config_manager_get_mqtt_pass(void)
{
    return current_config.mqtt_pass;
}

bool config_manager_save(const gateway_config_t *config)
{
    nvs_handle_t handle;

    if (config == NULL ||
        config->wifi_ssid[0] == '\0' ||
        config->mqtt_broker_uri[0] == '\0') {
        return false;
    }

    if (nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    if (nvs_set_str(handle, KEY_WIFI_SSID, config->wifi_ssid) != ESP_OK ||
        nvs_set_str(handle, KEY_WIFI_PASS, config->wifi_pass) != ESP_OK ||
        nvs_set_str(handle, KEY_MQTT_URI, config->mqtt_broker_uri) != ESP_OK ||
        nvs_set_str(handle, KEY_MQTT_USER, config->mqtt_user) != ESP_OK ||
        nvs_set_str(handle, KEY_MQTT_PASS, config->mqtt_pass) != ESP_OK ||
        nvs_commit(handle) != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    current_config = *config;
    config_loaded = true;
    factory_mode = false;
    ESP_LOGI(TAG, "Gateway config saved");
    return true;
}

bool config_manager_reset(void)
{
    nvs_handle_t handle;

    if (nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    if (nvs_erase_all(handle) != ESP_OK || nvs_commit(handle) != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    memset(&current_config, 0, sizeof(current_config));
    config_loaded = true;
    factory_mode = true;
    ESP_LOGI(TAG, "Gateway config cleared");
    return true;
}
