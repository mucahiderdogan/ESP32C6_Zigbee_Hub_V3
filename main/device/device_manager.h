#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAX_DEVICES 32

typedef struct
{
    char name[32];
    char type[32];
    char ieee[32];
    char manufacturer[32];
    char model[32];
    char features[128];
    uint16_t short_addr;
    uint16_t profile_id;
    uint16_t device_id;
    uint8_t endpoint;
} device_t;

void device_manager_init(void);

int device_manager_add(const char *name,
                       const char *type,
                       const char *ieee);

int device_manager_find_by_ieee(const char *ieee);
int device_manager_update_details(const char *ieee,
                                  uint16_t short_addr,
                                  uint8_t endpoint,
                                  uint16_t profile_id,
                                  uint16_t device_id,
                                  const char *type,
                                  const char *manufacturer,
                                  const char *model,
                                  const char *features);
bool device_manager_is_control(const device_t *device);

int device_manager_count(void);

device_t *device_manager_get(int index);

void device_manager_get_json(char *buffer, int max_len);
