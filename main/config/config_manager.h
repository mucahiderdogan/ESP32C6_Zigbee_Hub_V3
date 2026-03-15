#pragma once

#include <stdbool.h>

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char mqtt_broker_uri[128];
    char mqtt_user[65];
    char mqtt_pass[65];
} gateway_config_t;

void config_manager_init(void);
bool config_manager_is_factory_mode(void);
bool config_manager_get(gateway_config_t *config);
const char *config_manager_get_wifi_ssid(void);
const char *config_manager_get_wifi_pass(void);
const char *config_manager_get_mqtt_broker_uri(void);
const char *config_manager_get_mqtt_user(void);
const char *config_manager_get_mqtt_pass(void);
bool config_manager_save(const gateway_config_t *config);
bool config_manager_reset(void);
