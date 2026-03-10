#pragma once
#include <stdint.h>

typedef struct
{
    uint16_t short_addr;
    uint8_t ieee_addr[8];
} zigbee_device_t;

void zigbee_device_add(uint16_t short_addr, uint8_t ieee[8]);
zigbee_device_t* zigbee_device_get(uint16_t short_addr);
int zigbee_device_count(void);