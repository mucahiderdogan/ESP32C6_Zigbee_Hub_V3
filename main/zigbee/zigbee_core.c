#include "zigbee_core.h"

#include "config.h"
#include "device_manager.h"
#include "mqtt_bridge.h"
#include "web_server.h"

#include "esp_coexist.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_core.h"
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
#define ZIGBEE_MODEL_IDENTIFIER "\x09""ESP32C6Hub"
#define ZIGBEE_GATEWAY_ENDPOINT 1
#define ZIGBEE_PRIMARY_CHANNEL_MASK (1UL << ZIGBEE_CHANNEL)

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

static const char *TAG = "ZIGBEE";

static bool pairing_active;
static bool zigbee_ready;
static bool zigbee_started;

static void zigbee_format_ieee(const uint8_t *ieee_addr, char *buffer, size_t buffer_len)
{
    snprintf(buffer,
             buffer_len,
             "%02X%02X%02X%02X%02X%02X%02X%02X",
             ieee_addr[7], ieee_addr[6], ieee_addr[5], ieee_addr[4],
             ieee_addr[3], ieee_addr[2], ieee_addr[1], ieee_addr[0]);
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
    char device_name[32];

    if (esp_zb_ieee_address_by_short(short_addr, ieee_addr) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to resolve IEEE address for short 0x%04hx", short_addr);
        return;
    }

    zigbee_format_ieee(ieee_addr, ieee_string, sizeof(ieee_string));
    snprintf(device_name, sizeof(device_name), "Zigbee %04X", short_addr);

    if (device_manager_add(device_name, "zigbee", ieee_string) >= 0) {
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

static esp_err_t zigbee_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (callback_id == ESP_ZB_CORE_CMD_READ_ATTR_RESP_CB_ID) {
        const esp_zb_zcl_cmd_read_attr_resp_message_t *resp = message;
        esp_zb_zcl_read_attr_resp_variable_t *var;
        char ieee[17] = {0};
        char manufacturer[32] = {0};
        char model[32] = {0};
        uint8_t ieee_addr[8] = {0};

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

        if (device_manager_find_by_ieee(ieee) >= 0) {
            device_t *device = device_manager_get(device_manager_find_by_ieee(ieee));
            if (device != NULL) {
                mqtt_publish_joined_device(device->name, device->ieee);
            }
        }
    }

    return ESP_OK;
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
            ESP_LOGI(TAG, "Coordinator rebooted, network restored");
            web_log_send("Zigbee network restored");
        }
        break;

    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (status == ESP_OK)
        {
            zigbee_ready = true;
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
            zigbee_register_joined_device(params->device_short_addr);
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
        ESP_LOGI(TAG, "Zigbee signal %s (0x%x), status: %s",
                 esp_zb_zdo_signal_to_string(signal_type),
                 signal_type,
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
