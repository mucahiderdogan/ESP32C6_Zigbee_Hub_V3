#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mqtt_client.h"

void mqtt_start(void);
void mqtt_stop(void);
bool mqtt_is_running(void);

bool mqtt_publish_state(const char *topic,const char *payload);
bool mqtt_publish_device_joined(uint16_t short_addr,const uint8_t ieee_addr[8],uint8_t capability);
bool mqtt_publish_device_list_json(const char *json_payload);

esp_mqtt_client_handle_t mqtt_get_client(void);