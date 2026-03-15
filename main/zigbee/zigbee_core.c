#include "zigbee_core.h"

#include "config.h"
#include "device_manager.h"
#include "mqtt_bridge.h"
#include "web_server.h"

#include "esp_check.h"
#include "esp_coexist.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_core.h"
#include "zcl/esp_zigbee_zcl_ias_zone.h"
#include "zcl/esp_zigbee_zcl_occupancy_sensing.h"
#include "zdo/esp_zigbee_zdo_command.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZIGBEE_MAX_CHILDREN 10
#define ZIGBEE_NETWORK_SIZE 64
#define ZIGBEE_MANUFACTURER_CODE 0x131B
#define ZIGBEE_MANUFACTURER_NAME "\x09""ESPRESSIF"
#define ZIGBEE_MODEL_IDENTIFIER "\x0A""ESP32C6Hub"
#define ZIGBEE_GATEWAY_ENDPOINT 1
#define ZIGBEE_PRIMARY_CHANNEL_MASK (1UL << ZIGBEE_CHANNEL)
#define ZIGBEE_DEVICE_ONLINE_TIMEOUT_S 120
#define ZIGBEE_PRESENCE_TIMEOUT_S 30
#define ZIGBEE_PRESENCE_WATCHDOG_INTERVAL_MS 2000

#define ZIGBEE_COORDINATOR_CONFIG()          \
    {                                        \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR, \
        .install_code_policy = false,        \
        .nwk_cfg.zczr_cfg = {                \
            .max_children = ZIGBEE_MAX_CHILDREN, \
        },                                   \
    }

typedef struct {
    uint16_t short_addr;
    uint8_t endpoint;
    char ieee[32];
} zigbee_discovery_ctx_t;

typedef enum {
    ZIGBEE_PRESENCE_SOURCE_UNKNOWN = 0,
    ZIGBEE_PRESENCE_SOURCE_OCCUPANCY = 1,
    ZIGBEE_PRESENCE_SOURCE_IAS = 2,
} zigbee_presence_source_t;

typedef struct {
    bool used;
    bool online;
    bool known;
    bool occupied;
    int64_t last_seen_us;
    int64_t last_presence_us;
    int64_t last_change_us;
    char ieee[17];
} zigbee_presence_state_t;

static const char *TAG = "ZIGBEE";

static bool pairing_active;
static bool zigbee_ready;
static bool zigbee_started;
static zigbee_presence_state_t presence_states[MAX_DEVICES];

static void zigbee_format_ieee(const uint8_t *ieee_addr, char *buffer, size_t buffer_len);
static bool zigbee_ieee_by_short(uint16_t short_addr, char *ieee, size_t ieee_len);
static zigbee_presence_state_t *zigbee_presence_find(const char *ieee);
static zigbee_presence_state_t *zigbee_presence_slot(const char *ieee);
static void zigbee_seed_known_devices(void);
static void zigbee_touch_device(const char *ieee);
static void zigbee_touch_short(uint16_t short_addr);
static void zigbee_mark_offline_short(uint16_t short_addr);
static bool zigbee_is_unsupported_presence_device(const char *ieee);
static bool zigbee_device_has_feature(const char *ieee, const char *feature);
static void zigbee_restore_presence_reporting(void);
static void zigbee_refresh_presence_reporting(const char *ieee);
static bool zigbee_update_presence_state(const char *ieee,
                                         bool occupied,
                                         bool publish_output);
static void zigbee_configure_occupancy_reporting(uint16_t short_addr, uint8_t endpoint);
static const char *zigbee_signal_explanation(esp_zb_app_signal_type_t signal_type);
static void zigbee_presence_watchdog_task(void *pvParameters);

static void zigbee_format_ieee(const uint8_t *ieee_addr, char *buffer, size_t buffer_len)
{
    snprintf(buffer,
             buffer_len,
             "%02X%02X%02X%02X%02X%02X%02X%02X",
             ieee_addr[7], ieee_addr[6], ieee_addr[5], ieee_addr[4],
             ieee_addr[3], ieee_addr[2], ieee_addr[1], ieee_addr[0]);
}

static bool zigbee_ieee_by_short(uint16_t short_addr, char *ieee, size_t ieee_len)
{
    uint8_t ieee_addr[8] = {0};

    if (esp_zb_ieee_address_by_short(short_addr, ieee_addr) != ESP_OK) {
        return false;
    }

    zigbee_format_ieee(ieee_addr, ieee, ieee_len);
    return true;
}

static zigbee_presence_state_t *zigbee_presence_find(const char *ieee)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (presence_states[i].used &&
            strcmp(presence_states[i].ieee, ieee) == 0) {
            return &presence_states[i];
        }
    }

    return NULL;
}

static zigbee_presence_state_t *zigbee_presence_slot(const char *ieee)
{
    zigbee_presence_state_t *slot = zigbee_presence_find(ieee);

    if (slot != NULL) {
        return slot;
    }

    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!presence_states[i].used) {
            presence_states[i].used = true;
            presence_states[i].online = false;
            presence_states[i].known = false;
            presence_states[i].occupied = false;
            presence_states[i].last_seen_us = 0;
            presence_states[i].last_presence_us = 0;
            presence_states[i].last_change_us = 0;
            snprintf(presence_states[i].ieee, sizeof(presence_states[i].ieee), "%s", ieee);
            return &presence_states[i];
        }
    }

    return NULL;
}

static void zigbee_seed_known_devices(void)
{
    for (int i = 0; i < device_manager_count(); i++) {
        device_t *device = device_manager_get(i);
        zigbee_presence_state_t *slot;

        if (device == NULL || device_manager_is_control(device)) {
            continue;
        }

        slot = zigbee_presence_slot(device->ieee);
        if (slot == NULL) {
            continue;
        }

        slot->online = true;
    }
}

static void zigbee_touch_device(const char *ieee)
{
    zigbee_presence_state_t *slot;

    if (ieee == NULL || ieee[0] == '\0') {
        return;
    }

    slot = zigbee_presence_slot(ieee);
    if (slot == NULL) {
        return;
    }

    slot->online = true;
    slot->last_seen_us = esp_timer_get_time();
}

static void zigbee_touch_short(uint16_t short_addr)
{
    char ieee[17] = {0};

    if (!zigbee_ieee_by_short(short_addr, ieee, sizeof(ieee))) {
        return;
    }

    zigbee_touch_device(ieee);
}

static void zigbee_configure_occupancy_reporting(uint16_t short_addr, uint8_t endpoint)
{
    static esp_zb_zcl_config_report_record_t record = {
        .direction = ESP_ZB_ZCL_REPORT_DIRECTION_SEND,
        .attributeID = ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
        .attrType = ESP_ZB_ZCL_ATTR_TYPE_8BITMAP,
        .min_interval = 0,
        .max_interval = 30,
        .reportable_change = NULL,
    };

    esp_zb_zcl_config_report_cmd_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = short_addr,
            .dst_endpoint = endpoint,
            .src_endpoint = ZIGBEE_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID = ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .dis_default_resp = 1,
        .manuf_specific = 0,
        .manuf_code = EZP_ZB_ZCL_CLUSTER_NON_MANUFACTURER_SPECIFIC,
        .record_number = 1,
        .record_field = &record,
    };

    esp_zb_zcl_config_report_cmd_req(&cmd_req);
}

static void zigbee_mark_offline_short(uint16_t short_addr)
{
    char ieee[17] = {0};
    zigbee_presence_state_t *slot;

    if (!zigbee_ieee_by_short(short_addr, ieee, sizeof(ieee))) {
        return;
    }

    slot = zigbee_presence_find(ieee);
    if (slot == NULL) {
        return;
    }

    slot->online = false;
}

static bool zigbee_is_unsupported_presence_device(const char *ieee)
{
    int index;
    device_t *device;

    if (ieee == NULL || ieee[0] == '\0') {
        return false;
    }

    index = device_manager_find_by_ieee(ieee);
    if (index < 0) {
        return false;
    }

    device = device_manager_get(index);
    if (device == NULL) {
        return false;
    }

    return strcmp(device->model, "SNZB-06P") == 0;
}

static bool zigbee_device_has_feature(const char *ieee, const char *feature)
{
    int index;
    device_t *device;

    if (ieee == NULL || feature == NULL) {
        return false;
    }

    index = device_manager_find_by_ieee(ieee);
    if (index < 0) {
        return false;
    }

    device = device_manager_get(index);
    if (device == NULL) {
        return false;
    }

    return strstr(device->features, feature) != NULL;
}

static void zigbee_restore_presence_reporting(void)
{
    for (int i = 0; i < device_manager_count(); i++) {
        device_t *device = device_manager_get(i);

        if (device == NULL || device_manager_is_control(device)) {
            continue;
        }

        if (device->short_addr == 0 || device->endpoint == 0) {
            continue;
        }

        if (!zigbee_device_has_feature(device->ieee, "0x0406")) {
            continue;
        }

        if (zigbee_is_unsupported_presence_device(device->ieee)) {
            continue;
        }

        zigbee_configure_occupancy_reporting(device->short_addr, device->endpoint);
    }
}

static void zigbee_refresh_presence_reporting(const char *ieee)
{
    int index;
    device_t *device;

    if (ieee == NULL || ieee[0] == '\0') {
        return;
    }

    index = device_manager_find_by_ieee(ieee);
    if (index < 0) {
        return;
    }

    device = device_manager_get(index);
    if (device == NULL) {
        return;
    }

    if (device->short_addr == 0 || device->endpoint == 0) {
        return;
    }

    if (!zigbee_device_has_feature(ieee, "0x0406")) {
        return;
    }

    if (zigbee_is_unsupported_presence_device(ieee)) {
        return;
    }

    zigbee_configure_occupancy_reporting(device->short_addr, device->endpoint);
}

bool zigbee_core_is_device_online(const char *ieee)
{
    zigbee_presence_state_t *slot;

    if (ieee == NULL || ieee[0] == '\0') {
        return false;
    }

    slot = zigbee_presence_find(ieee);
    if (slot == NULL) {
        return false;
    }

    return slot->online;
}

uint32_t zigbee_core_device_last_seen_seconds(const char *ieee)
{
    zigbee_presence_state_t *slot;
    int64_t now_us;
    int64_t diff_us;

    if (ieee == NULL || ieee[0] == '\0') {
        return 0;
    }

    slot = zigbee_presence_find(ieee);
    if (slot == NULL || slot->last_seen_us <= 0) {
        return 0;
    }

    now_us = esp_timer_get_time();
    diff_us = now_us - slot->last_seen_us;
    if (diff_us <= 0) {
        return 0;
    }

    return (uint32_t)(diff_us / 1000000LL);
}

static bool zigbee_update_presence_state(const char *ieee,
                                         bool occupied,
                                         bool publish_output)
{
    zigbee_presence_state_t *slot;
    device_t *device = NULL;
    int index = device_manager_find_by_ieee(ieee);
    int64_t now_us = esp_timer_get_time();
    const char *name = ieee;
    char message[128];

    slot = zigbee_presence_slot(ieee);
    if (slot == NULL) {
        return false;
    }

    if (occupied && (!slot->known || !slot->occupied)) {
        slot->last_presence_us = now_us;
    } else if (!occupied) {
        slot->last_presence_us = 0;
    }

    if (slot->known && slot->occupied == occupied) {
        return false;
    }

    if (index >= 0) {
        device = device_manager_get(index);
        if (device != NULL && device->name[0] != '\0') {
            name = device->name;
        }
    }

    slot->known = true;
    slot->occupied = occupied;
    slot->last_change_us = now_us;

    if (!publish_output) {
        return false;
    }

    snprintf(message,
             sizeof(message),
             "%s: %s",
             name,
             occupied ? "varlik algilandi" : "varlik algilanmadi");
    ESP_LOGI(TAG, "%s", message);
    return true;
}

static void zigbee_publish_presence_short(uint16_t short_addr,
                                          bool occupied,
                                          zigbee_presence_source_t source)
{
    (void)source;
    char ieee[17] = {0};

    if (!zigbee_ieee_by_short(short_addr, ieee, sizeof(ieee))) {
        return;
    }

    zigbee_touch_device(ieee);

    if (zigbee_is_unsupported_presence_device(ieee)) {
        return;
    }

    if (zigbee_update_presence_state(ieee, occupied, true)) {
        mqtt_publish_device_presence(ieee, occupied);
    }
}

static void zigbee_presence_watchdog_task(void *pvParameters)
{
    (void)pvParameters;

    while (true) {
        int64_t now_us = esp_timer_get_time();

        for (int i = 0; i < MAX_DEVICES; i++) {
            zigbee_presence_state_t *slot = &presence_states[i];
            int64_t diff_us;

            if (!slot->used || !slot->occupied) {
                continue;
            }

            if (!zigbee_device_has_feature(slot->ieee, "0x0406")) {
                continue;
            }

            if (zigbee_is_unsupported_presence_device(slot->ieee)) {
                continue;
            }

            if (slot->last_presence_us <= 0) {
                continue;
            }

            diff_us = now_us - slot->last_presence_us;
            if (diff_us <= ((int64_t)ZIGBEE_PRESENCE_TIMEOUT_S * 1000000LL)) {
                continue;
            }

            if (zigbee_update_presence_state(slot->ieee, false, true)) {
                mqtt_publish_device_presence(slot->ieee, false);
                zigbee_refresh_presence_reporting(slot->ieee);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(ZIGBEE_PRESENCE_WATCHDOG_INTERVAL_MS));
    }
}

static bool zigbee_has_cluster(const esp_zb_af_simple_desc_1_1_t *simple_desc, uint16_t cluster_id)
{
    uint8_t count = simple_desc->app_input_cluster_count + simple_desc->app_output_cluster_count;

    for (uint8_t i = 0; i < count; i++) {
        if (simple_desc->app_cluster_list[i] == cluster_id) {
            return true;
        }
    }

    return false;
}

static void zigbee_build_features(const esp_zb_af_simple_desc_1_1_t *simple_desc,
                                  char *buffer,
                                  size_t buffer_len)
{
    size_t offset = 0;
    uint8_t count = simple_desc->app_input_cluster_count + simple_desc->app_output_cluster_count;

    for (uint8_t i = 0; i < count && offset < buffer_len; i++) {
        offset += snprintf(buffer + offset,
                           buffer_len - offset,
                           "%s0x%04X",
                           (i == 0) ? "" : ",",
                           simple_desc->app_cluster_list[i]);
    }
}

static const char *zigbee_infer_type(const esp_zb_af_simple_desc_1_1_t *simple_desc)
{
    if (zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL) ||
        zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL)) {
        return "light";
    }

    if (zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF)) {
        return "switch";
    }

    if (zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_IAS_ZONE) ||
        zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING)) {
        return "binary_sensor";
    }

    if (zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT) ||
        zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT) ||
        zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT) ||
        zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT) ||
        zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT)) {
        return "sensor";
    }

    if (zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT)) {
        return "climate";
    }

    if (zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING)) {
        return "cover";
    }

    return "zigbee";
}

static void zigbee_request_basic_attributes(const zigbee_discovery_ctx_t *ctx)
{
    static uint16_t attr_ids[] = {
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
    };

    esp_zb_zcl_read_attr_cmd_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = ctx->short_addr,
            .dst_endpoint = ctx->endpoint,
            .src_endpoint = ZIGBEE_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID = ESP_ZB_ZCL_CLUSTER_ID_BASIC,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .dis_default_resp = 1,
        .manuf_specific = 0,
        .manuf_code = EZP_ZB_ZCL_CLUSTER_NON_MANUFACTURER_SPECIFIC,
        .attr_number = 2,
        .attr_field = attr_ids,
    };

    esp_zb_zcl_read_attr_cmd_req(&cmd_req);
}

static void zigbee_simple_desc_cb(esp_zb_zdp_status_t zdo_status,
                                  esp_zb_af_simple_desc_1_1_t *simple_desc,
                                  void *user_ctx)
{
    zigbee_discovery_ctx_t *ctx = user_ctx;

    if (ctx == NULL) {
        return;
    }

    if (zdo_status == ESP_ZB_ZDP_STATUS_SUCCESS && simple_desc != NULL) {
        char features[128] = {0};
        const char *type = zigbee_infer_type(simple_desc);

        zigbee_build_features(simple_desc, features, sizeof(features));
        device_manager_update_details(ctx->ieee,
                                      ctx->short_addr,
                                      simple_desc->endpoint,
                                      simple_desc->app_profile_id,
                                      simple_desc->app_device_id,
                                      type,
                                      NULL,
                                      NULL,
                                      features);

        ESP_LOGI(TAG,
                 "Discovered %s ep=%u profile=0x%04x device=0x%04x clusters=%s",
                 ctx->ieee,
                 simple_desc->endpoint,
                 simple_desc->app_profile_id,
                 simple_desc->app_device_id,
                 features);

        ctx->endpoint = simple_desc->endpoint;
        if (zigbee_has_cluster(simple_desc, ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING)) {
            zigbee_configure_occupancy_reporting(ctx->short_addr, simple_desc->endpoint);
        }
        zigbee_request_basic_attributes(ctx);
    } else {
        ESP_LOGW(TAG, "Simple descriptor request failed for %s, status=%d", ctx->ieee, zdo_status);
    }

    free(ctx);
}

static void zigbee_active_ep_cb(esp_zb_zdp_status_t zdo_status,
                                uint8_t ep_count,
                                uint8_t *ep_id_list,
                                void *user_ctx)
{
    zigbee_discovery_ctx_t *ctx = user_ctx;

    if (ctx == NULL) {
        return;
    }

    if (zdo_status != ESP_ZB_ZDP_STATUS_SUCCESS || ep_count == 0 || ep_id_list == NULL) {
        ESP_LOGW(TAG, "Active endpoint discovery failed for %s, status=%d", ctx->ieee, zdo_status);
        free(ctx);
        return;
    }

    ctx->endpoint = ep_id_list[0];

    esp_zb_zdo_simple_desc_req_param_t req = {
        .addr_of_interest = ctx->short_addr,
        .endpoint = ctx->endpoint,
    };

    esp_zb_zdo_simple_desc_req(&req, zigbee_simple_desc_cb, ctx);
}

static void zigbee_request_device_details(uint16_t short_addr, const char *ieee)
{
    zigbee_discovery_ctx_t *ctx = calloc(1, sizeof(zigbee_discovery_ctx_t));

    if (ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate discovery context");
        return;
    }

    ctx->short_addr = short_addr;
    snprintf(ctx->ieee, sizeof(ctx->ieee), "%s", ieee);

    esp_zb_zdo_active_ep_req_param_t req = {
        .addr_of_interest = short_addr,
    };

    esp_zb_zdo_active_ep_req(&req, zigbee_active_ep_cb, ctx);
}

static void zigbee_register_joined_device(uint16_t short_addr)
{
    uint8_t ieee_addr[8] = {0};
    char ieee_string[17];

    if (esp_zb_ieee_address_by_short(short_addr, ieee_addr) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to resolve IEEE address for short 0x%04hx", short_addr);
        return;
    }

    zigbee_format_ieee(ieee_addr, ieee_string, sizeof(ieee_string));
    zigbee_touch_device(ieee_string);

    if (device_manager_add(ieee_string, "zigbee", ieee_string) >= 0) {
        ESP_LOGI(TAG, "Registered joined device %s", ieee_string);
        web_log_send("Zigbee device joined");
    } else {
        ESP_LOGW(TAG, "Device registry full, cannot add %s", ieee_string);
        web_log_send("Device registry full");
        return;
    }

    zigbee_request_device_details(short_addr, ieee_string);
}

static void zigbee_start_bdb_commissioning(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK,
                        ,
                        TAG,
                        "Failed to start Zigbee BDB commissioning");
}

static void zigbee_copy_zcl_string(char *dest, size_t dest_len, const void *value_ptr)
{
    const uint8_t *zb_str = value_ptr;
    size_t length;

    if (dest_len == 0 || value_ptr == NULL) {
        return;
    }

    length = zb_str[0];
    if (length >= dest_len) {
        length = dest_len - 1;
    }

    memcpy(dest, zb_str + 1, length);
    dest[length] = '\0';
}

static void zigbee_build_display_name(char *dest,
                                      size_t dest_len,
                                      const char *manufacturer,
                                      const char *model)
{
    if (dest == NULL || dest_len == 0) {
        return;
    }

    dest[0] = '\0';

    if (manufacturer != NULL && manufacturer[0] != '\0' &&
        model != NULL && model[0] != '\0') {
        snprintf(dest, dest_len, "%s %s", manufacturer, model);
        return;
    }

    if (model != NULL && model[0] != '\0') {
        snprintf(dest, dest_len, "%s", model);
        return;
    }

    if (manufacturer != NULL && manufacturer[0] != '\0') {
        snprintf(dest, dest_len, "%s", manufacturer);
    }
}

static esp_err_t zigbee_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (callback_id == ESP_ZB_CORE_REPORT_ATTR_CB_ID) {
        const esp_zb_zcl_report_attr_message_t *report = message;

        if (report != NULL &&
            report->src_address.addr_type == ESP_ZB_ZCL_ADDR_TYPE_SHORT &&
            report->cluster == ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING &&
            report->attribute.id == ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID &&
            report->attribute.data.value != NULL) {
            uint8_t occupancy = *((uint8_t *)report->attribute.data.value);
            bool occupied = (occupancy & ESP_ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_OCCUPIED) != 0;
            zigbee_publish_presence_short(report->src_address.u.short_addr,
                                          occupied,
                                          ZIGBEE_PRESENCE_SOURCE_OCCUPANCY);
        } else if (report != NULL &&
                   report->src_address.addr_type == ESP_ZB_ZCL_ADDR_TYPE_SHORT) {
            zigbee_touch_short(report->src_address.u.short_addr);
        }
        return ESP_OK;
    }

    if (callback_id == ESP_ZB_CORE_CMD_IAS_ZONE_ZONE_STATUS_CHANGE_NOT_ID) {
        const esp_zb_zcl_ias_zone_status_change_notification_message_t *zone = message;

        if (zone != NULL && zone->info.src_address.addr_type == ESP_ZB_ZCL_ADDR_TYPE_SHORT) {
            bool occupied = (zone->zone_status & ESP_ZB_ZCL_IAS_ZONE_ZONE_STATUS_ALARM1) != 0;
            zigbee_publish_presence_short(zone->info.src_address.u.short_addr,
                                          occupied,
                                          ZIGBEE_PRESENCE_SOURCE_IAS);
        }
        return ESP_OK;
    }

    if (callback_id == ESP_ZB_CORE_CMD_READ_ATTR_RESP_CB_ID) {
        const esp_zb_zcl_cmd_read_attr_resp_message_t *resp = message;
        esp_zb_zcl_read_attr_resp_variable_t *var;
        char ieee[17] = {0};
        char manufacturer[32] = {0};
        char model[32] = {0};
        char display_name[32] = {0};
        uint8_t ieee_addr[8] = {0};
        int index;
        device_t *device = NULL;
        const char *device_type = "zigbee";

        if (resp == NULL || resp->info.src_address.addr_type != ESP_ZB_ZCL_ADDR_TYPE_SHORT) {
            return ESP_OK;
        }

        if (esp_zb_ieee_address_by_short(resp->info.src_address.u.short_addr, ieee_addr) != ESP_OK) {
            return ESP_OK;
        }

        zigbee_format_ieee(ieee_addr, ieee, sizeof(ieee));
        for (var = resp->variables; var != NULL; var = var->next) {
            if (var->status != ESP_ZB_ZCL_STATUS_SUCCESS) {
                continue;
            }

            if (var->attribute.id == ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID) {
                zigbee_copy_zcl_string(manufacturer, sizeof(manufacturer), var->attribute.data.value);
            } else if (var->attribute.id == ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID) {
                zigbee_copy_zcl_string(model, sizeof(model), var->attribute.data.value);
            }
        }

        zigbee_touch_short(resp->info.src_address.u.short_addr);

        index = device_manager_find_by_ieee(ieee);
        if (index >= 0) {
            device = device_manager_get(index);
            if (device != NULL && device->type[0] != '\0') {
                device_type = device->type;
            }
        }

        zigbee_build_display_name(display_name,
                                  sizeof(display_name),
                                  manufacturer,
                                  model);
        if (display_name[0] != '\0') {
            device_manager_add(display_name, device_type, ieee);
        }

        device_manager_update_details(ieee,
                                      resp->info.src_address.u.short_addr,
                                      resp->info.src_endpoint,
                                      0,
                                      0,
                                      NULL,
                                      manufacturer,
                                      model,
                                      NULL);

        if (manufacturer[0] != '\0' || model[0] != '\0') {
            ESP_LOGI(TAG, "Device %s manufacturer=%s model=%s", ieee, manufacturer, model);
        }

        index = device_manager_find_by_ieee(ieee);
        if (index >= 0) {
            device = device_manager_get(index);
            if (device != NULL) {
                mqtt_publish_joined_device(device->name, device->ieee);
            }
        }
    }

    return ESP_OK;
}

static const char *zigbee_signal_explanation(esp_zb_app_signal_type_t signal_type)
{
    switch ((uint32_t)signal_type) {
    case 0x2F:
        return "Cihaz aga yetkilendirildi";
    case 0x30:
        return "Cihaz guncellemesi alindi";
    case 0x32:
        return "Ag katman durum bildirimi (NLME)";
    case 0x12:
        return "Cihaz aga iliskilendirildi";
    case 0x13:
        return "Cihaz agdan ayrildi";
    case 0x36:
        return "Pair (permit join) durumu degisti";
    default:
        return "Genel Zigbee olayi";
    }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *signal = signal_struct->p_app_signal;
    esp_err_t status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t signal_type = *signal;

    switch (signal_type)
    {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_ERROR_CHECK(esp_coex_wifi_i154_enable());
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
        ESP_LOGI(TAG, "Initializing Zigbee stack");
        zigbee_start_bdb_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (status != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize Zigbee stack: %s", esp_err_to_name(status));
            break;
        }

        if (esp_zb_bdb_is_factory_new())
        {
            ESP_LOGI(TAG, "Factory-new coordinator, starting network formation");
            zigbee_start_bdb_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
        }
        else
        {
            zigbee_ready = true;
            zigbee_restore_presence_reporting();
            ESP_LOGI(TAG, "Coordinator rebooted, network restored");
            web_log_send("Zigbee network restored");
        }
        break;

    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (status == ESP_OK)
        {
            zigbee_ready = true;
            zigbee_restore_presence_reporting();
            ESP_LOGI(TAG,
                     "Zigbee network formed PAN ID 0x%04hx channel %d short 0x%04hx",
                     esp_zb_get_pan_id(),
                     esp_zb_get_current_channel(),
                     esp_zb_get_short_address());
            web_log_send("Zigbee network formed");
        }
        else
        {
            ESP_LOGW(TAG, "Network formation failed, retrying");
            esp_zb_scheduler_alarm((esp_zb_callback_t)zigbee_start_bdb_commissioning,
                                   ESP_ZB_BDB_MODE_NETWORK_FORMATION,
                                   1000);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
        if (status == ESP_OK)
        {
            esp_zb_zdo_signal_device_annce_params_t *params =
                (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(signal);
            ESP_LOGI(TAG, "Device announced short address 0x%04hx", params->device_short_addr);
            zigbee_touch_short(params->device_short_addr);
            zigbee_register_joined_device(params->device_short_addr);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_LEAVE_INDICATION:
        if (status == ESP_OK)
        {
            esp_zb_zdo_signal_leave_indication_params_t *params =
                (esp_zb_zdo_signal_leave_indication_params_t *)esp_zb_app_signal_get_params(signal);
            zigbee_mark_offline_short(params->short_addr);
            ESP_LOGI(TAG, "Device 0x%04hx left network", params->short_addr);
        }
        break;

    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (status == ESP_OK)
        {
            uint8_t remaining = *(uint8_t *)esp_zb_app_signal_get_params(signal);
            pairing_active = remaining > 0;
            if (pairing_active)
            {
                ESP_LOGI(TAG, "Network open for %u seconds", remaining);
                web_log_send("Pair mode enabled");
            }
            else
            {
                ESP_LOGI(TAG, "Network closed for joining");
                web_log_send("Pair mode disabled");
            }

            mqtt_publish_pair_state(pairing_active);
        }
        break;

    default:
        ESP_LOGI(TAG, "Zigbee signal %s (0x%x): %s, status: %s",
                 esp_zb_zdo_signal_to_string(signal_type),
                 signal_type,
                 zigbee_signal_explanation(signal_type),
                 esp_err_to_name(status));
        break;
    }
}

static void zigbee_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ZIGBEE_COORDINATOR_CONFIG();
    esp_zb_ep_list_t *ep_list;
    esp_zb_cluster_list_t *cluster_list;
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = ZIGBEE_GATEWAY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_zb_overall_network_size_set(ZIGBEE_NETWORK_SIZE);
    esp_zb_init(&zb_nwk_cfg);
    ESP_ERROR_CHECK(esp_zb_set_primary_network_channel_set(ZIGBEE_PRIMARY_CHANNEL_MASK));

    ep_list = esp_zb_ep_list_create();
    cluster_list = esp_zb_zcl_cluster_list_create();

    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)ZIGBEE_MANUFACTURER_NAME));
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)ZIGBEE_MODEL_IDENTIFIER));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_ep_list_add_gateway_ep(ep_list, cluster_list, endpoint_config));
    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));

    esp_zb_set_node_descriptor_manufacturer_code(ZIGBEE_MANUFACTURER_CODE);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void zigbee_core_init(void)
{
    if (zigbee_started)
    {
        ESP_LOGW(TAG, "Zigbee core already started");
        return;
    }

    pairing_active = false;
    zigbee_ready = false;
    zigbee_started = true;
    memset(presence_states, 0, sizeof(presence_states));
    zigbee_seed_known_devices();

    esp_zb_platform_config_t platform_config = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
        },
    };

    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_config));
    esp_zb_core_action_handler_register(zigbee_action_handler);
    xTaskCreate(zigbee_task, "Zigbee_main", 8192, NULL, 5, NULL);
    xTaskCreate(zigbee_presence_watchdog_task, "Zigbee_presence", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "Core initialized on channel %d", ZIGBEE_CHANNEL);
}

bool zigbee_core_start_pairing(void)
{
    if (!zigbee_ready)
    {
        ESP_LOGW(TAG, "Zigbee network is not ready");
        web_log_send("Zigbee network is not ready");
        return false;
    }

    if (pairing_active)
    {
        ESP_LOGW(TAG, "Pair mode already active");
        web_log_send("Pair mode already active");
        return false;
    }

    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(1000)))
    {
        ESP_LOGE(TAG, "Failed to acquire Zigbee lock");
        return false;
    }

    esp_err_t err = esp_zb_bdb_open_network(PERMIT_JOIN_SEC);
    esp_zb_lock_release();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open Zigbee network: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

bool zigbee_core_is_pairing_active(void)
{
    return pairing_active;
}
