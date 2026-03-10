
#pragma once

#include <stdbool.h>
#include <stdint.h>

void wifi_init(void);
bool wifi_is_connected(void);
bool wifi_wait_connected(uint32_t timeout_ms);
