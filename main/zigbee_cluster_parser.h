#pragma once
#include <stdint.h>

void zigbee_parse_attribute(uint16_t cluster,
                            uint16_t attr,
                            uint16_t short_addr,
                            uint8_t *data);