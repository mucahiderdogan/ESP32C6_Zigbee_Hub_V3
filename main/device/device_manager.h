#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAX_DEVICES 32

typedef struct
{
    char name[32];
    char type[32];
    char ieee[32];
} device_t;

void device_manager_init(void);

int device_manager_add(const char *name,
                       const char *type,
                       const char *ieee);

int device_manager_find_by_ieee(const char *ieee);
bool device_manager_is_control(const device_t *device);

int device_manager_count(void);

device_t *device_manager_get(int index);

void device_manager_get_json(char *buffer, int max_len);
