#include "device_manager.h"

#include "nvs.h"

#include <stdio.h>
#include <string.h>

#define DEVICE_NAMESPACE "devices"
#define DEVICE_COUNT_KEY "count"
#define CONTROL_PREFIX   "ctrl_"

static device_t devices[MAX_DEVICES];
static int device_count;

static void device_manager_save(void)
{
    nvs_handle_t handle;

    if (nvs_open(DEVICE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_i32(handle, DEVICE_COUNT_KEY, device_count);

    for (int i = 0; i < device_count; i++) {
        char key[16];

        snprintf(key, sizeof(key), "dev%02d", i);
        nvs_set_blob(handle, key, &devices[i], sizeof(device_t));
    }

    nvs_commit(handle);
    nvs_close(handle);
}

static void device_manager_load(void)
{
    nvs_handle_t handle;
    int32_t saved_count = 0;

    if (nvs_open(DEVICE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    if (nvs_get_i32(handle, DEVICE_COUNT_KEY, &saved_count) != ESP_OK ||
        saved_count < 0 ||
        saved_count > MAX_DEVICES) {
        nvs_close(handle);
        return;
    }

    for (int i = 0; i < saved_count; i++) {
        char key[16];
        size_t size = sizeof(device_t);

        snprintf(key, sizeof(key), "dev%02d", i);
        if (nvs_get_blob(handle, key, &devices[i], &size) != ESP_OK || size != sizeof(device_t)) {
            break;
        }

        device_count++;
    }

    nvs_close(handle);
}

void device_manager_init(void)
{
    memset(devices, 0, sizeof(devices));
    device_count = 0;
    device_manager_load();
}

int device_manager_add(const char *name,
                       const char *type,
                       const char *ieee)
{
    int existing_index = device_manager_find_by_ieee(ieee);

    if (existing_index >= 0) {
        snprintf(devices[existing_index].name, sizeof(devices[existing_index].name), "%s", name);
        snprintf(devices[existing_index].type, sizeof(devices[existing_index].type), "%s", type);
        device_manager_save();
        return existing_index;
    }

    if (device_count >= MAX_DEVICES) {
        return -1;
    }

    snprintf(devices[device_count].name, sizeof(devices[device_count].name), "%s", name);
    snprintf(devices[device_count].type, sizeof(devices[device_count].type), "%s", type);
    snprintf(devices[device_count].ieee, sizeof(devices[device_count].ieee), "%s", ieee);

    device_count++;
    device_manager_save();

    return device_count - 1;
}

int device_manager_find_by_ieee(const char *ieee)
{
    for (int i = 0; i < device_count; i++) {
        if (strncmp(devices[i].ieee, ieee, sizeof(devices[i].ieee)) == 0) {
            return i;
        }
    }

    return -1;
}

bool device_manager_remove_by_ieee(const char *ieee)
{
    int index = device_manager_find_by_ieee(ieee);

    if (index < 0) {
        return false;
    }

    for (int i = index; i < device_count - 1; i++) {
        devices[i] = devices[i + 1];
    }

    memset(&devices[device_count - 1], 0, sizeof(device_t));
    device_count--;
    device_manager_save();

    return true;
}

int device_manager_update_details(const char *ieee,
                                  uint16_t short_addr,
                                  uint8_t endpoint,
                                  uint16_t profile_id,
                                  uint16_t device_id,
                                  const char *type,
                                  const char *manufacturer,
                                  const char *model,
                                  const char *features)
{
    int index = device_manager_find_by_ieee(ieee);

    if (index < 0) {
        return -1;
    }

    devices[index].short_addr = short_addr;
    devices[index].endpoint = endpoint;
    devices[index].profile_id = profile_id;
    devices[index].device_id = device_id;

    if (type != NULL && type[0] != '\0') {
        snprintf(devices[index].type, sizeof(devices[index].type), "%s", type);
    }

    if (manufacturer != NULL && manufacturer[0] != '\0') {
        snprintf(devices[index].manufacturer, sizeof(devices[index].manufacturer), "%s", manufacturer);
    }

    if (model != NULL && model[0] != '\0') {
        snprintf(devices[index].model, sizeof(devices[index].model), "%s", model);
    }

    if (features != NULL && features[0] != '\0') {
        snprintf(devices[index].features, sizeof(devices[index].features), "%s", features);
    }

    device_manager_save();
    return index;
}

bool device_manager_is_control(const device_t *device)
{
    if (device == NULL) {
        return false;
    }

    return strncmp(device->ieee, CONTROL_PREFIX, strlen(CONTROL_PREFIX)) == 0;
}

int device_manager_count(void)
{
    return device_count;
}

device_t *device_manager_get(int index)
{
    if (index < 0 || index >= device_count) {
        return NULL;
    }

    return &devices[index];
}

void device_manager_get_json(char *buffer, int max_len)
{
    int offset = 0;

    offset += snprintf(buffer + offset, max_len - offset, "{ \"devices\": [");

    for (int i = 0; i < device_count; i++) {
        offset += snprintf(buffer + offset,
                           max_len - offset,
                           "{ \"name\":\"%s\", \"type\":\"%s\", \"ieee\":\"%s\", \"manufacturer\":\"%s\", \"model\":\"%s\", \"features\":\"%s\", \"endpoint\":%u, \"short_addr\":%u, \"device_id\":%u }%s",
                           devices[i].name,
                           devices[i].type,
                           devices[i].ieee,
                           devices[i].manufacturer,
                           devices[i].model,
                           devices[i].features,
                           devices[i].endpoint,
                           devices[i].short_addr,
                           devices[i].device_id,
                           (i < device_count - 1) ? "," : "");
    }

    snprintf(buffer + offset, max_len - offset, "] }");
}
