#pragma once
#include <stdint.h>

void ha_discovery_publish_device(uint16_t short_addr,
                                 const uint8_t ieee_addr[8]);