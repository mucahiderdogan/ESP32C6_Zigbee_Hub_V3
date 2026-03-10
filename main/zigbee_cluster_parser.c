#include "zigbee_cluster_parser.h"
#include "mqtt.h"
#include <stdio.h>

void zigbee_parse_attribute(uint16_t cluster,
                            uint16_t attr,
                            uint16_t short_addr,
                            uint8_t *data)
{
    char topic[64];
    char payload[128];

    sprintf(topic,"zigbee/device/%04x/state",short_addr);

    switch(cluster)
    {
        case 0x0006:
        {
            int state=data[0];
            sprintf(payload,"{\"state\":\"%s\"}",state?"ON":"OFF");
            mqtt_publish(topic,payload);
            break;
        }

        case 0x0402:
        {
            int16_t raw=(data[1]<<8)|data[0];
            float temp=raw/100.0;

            sprintf(payload,"{\"temperature\":%.2f}",temp);
            mqtt_publish(topic,payload);
            break;
        }

        case 0x0405:
        {
            int raw=(data[1]<<8)|data[0];
            float hum=raw/100.0;

            sprintf(payload,"{\"humidity\":%.2f}",hum);
            mqtt_publish(topic,payload);
            break;
        }
    }
}