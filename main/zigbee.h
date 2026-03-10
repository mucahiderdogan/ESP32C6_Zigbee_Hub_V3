#pragma once

#include <stdint.h>

void zigbee_init(void);
void zigbee_send_onoff(uint16_t short_addr,const char *cmd);
void zigbee_build_device_json(char *buf,int size);