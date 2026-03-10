#include "device_manager.h"

#include <string.h>
#include <stdio.h>

static device_t devices[MAX_DEVICES];
static int device_count = 0;


/* =========================
   INIT
   ========================= */

void device_manager_init(void)
{
    device_count = 0;
}


/* =========================
   ADD DEVICE
   ========================= */

int device_manager_add(const char *name,
                       const char *type,
                       const char *ieee)
{
    if (device_count >= MAX_DEVICES)
        return -1;

    strncpy(devices[device_count].name, name, sizeof(devices[device_count].name));
    strncpy(devices[device_count].type, type, sizeof(devices[device_count].type));
    strncpy(devices[device_count].ieee, ieee, sizeof(devices[device_count].ieee));

    device_count++;

    return device_count - 1;
}


/* =========================
   COUNT
   ========================= */

int device_manager_count(void)
{
    return device_count;
}


/* =========================
   GET DEVICE
   ========================= */

device_t *device_manager_get(int index)
{
    if (index >= device_count)
        return NULL;

    return &devices[index];
}


/* =========================
   JSON EXPORT
   ========================= */

void device_manager_get_json(char *buffer, int max_len)
{
    int offset = 0;

    offset += snprintf(buffer + offset,
                       max_len - offset,
                       "{ \"devices\": [");

    for (int i = 0; i < device_count; i++)
    {
        offset += snprintf(buffer + offset,
                           max_len - offset,
                           "{ \"name\":\"%s\", \"type\":\"%s\" }%s",
                           devices[i].name,
                           devices[i].type,
                           (i < device_count - 1) ? "," : "");
    }

    snprintf(buffer + offset,
             max_len - offset,
             "] }");
}