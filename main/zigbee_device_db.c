#include "zigbee_device_db.h"
#include <string.h>

#define MAX_ZB_DEVICES 32

static zigbee_device_t devices[MAX_ZB_DEVICES];
static int device_count=0;

void zigbee_device_add(uint16_t short_addr,uint8_t ieee[8])
{
    if(device_count>=MAX_ZB_DEVICES) return;

    devices[device_count].short_addr=short_addr;
    memcpy(devices[device_count].ieee_addr,ieee,8);

    device_count++;
}

zigbee_device_t* zigbee_device_get(uint16_t short_addr)
{
    for(int i=0;i<device_count;i++)
        if(devices[i].short_addr==short_addr)
            return &devices[i];

    return 0;
}

int zigbee_device_count()
{
    return device_count;
}