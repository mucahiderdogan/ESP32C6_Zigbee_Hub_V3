#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0

extern EventGroupHandle_t wifi_event_group;

void wifi_manager_init(void);
bool wifi_manager_wait_until_ready(TickType_t timeout_ticks);
