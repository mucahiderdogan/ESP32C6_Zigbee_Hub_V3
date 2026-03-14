#include "web_server.h"
#include "device_manager.h"
#include "mqtt_bridge.h"
#include "zigbee_core.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WEB";
static const char *PAIR_CMD = "pair";

static httpd_handle_t server = NULL;
static int ws_client_fd = -1;

/* =========================
   LOG GÖNDER
   ========================= */

void web_log_send(const char *msg)
{
    if (server == NULL || ws_client_fd < 0)
        return;

    httpd_ws_frame_t frame;

    memset(&frame, 0, sizeof(frame));

    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)msg;
    frame.len = strlen(msg);

    httpd_ws_send_frame_async(server, ws_client_fd, &frame);
}

/* =========================
   ROOT PAGE
   ========================= */

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *resp =
        "<html>"
        "<head>"
        "<title>Zigbee Gateway</title>"
        "<style>"
        "body{font-family:Arial;background:#111;color:#eee;margin:0}"
        "header{padding:20px;text-align:center;background:#222}"
        ".container{padding:20px}"
        ".devices{width:100%}"
        "button{padding:6px 10px;font-size:14px}"
        "table{width:100%;border-collapse:collapse}"
        "th,td{border-bottom:1px solid #333;padding:10px;text-align:left}"
        "</style>"
        "</head>"

        "<body>"

        "<header>"
        "<h1>ESP32 Zigbee Gateway</h1>"
        "</header>"

        "<div class='container'>"

        "<div class='devices'>"
        "<h2>Devices</h2>"
        "<table id='devices'>"
        "<tr><th>Name</th><th>Type</th><th>Status</th><th>Action</th></tr>"
        "</table>"
        "</div>"

        "</div>"

        "<script>"

        "function deleteDevice(ieee){"
        "fetch('/device/delete?ieee='+encodeURIComponent(ieee),{method:'POST'})"
        ".then(()=>loadDevices());"
        "}"

        /* load devices */

        "function loadDevices(){"
        "fetch('/devices').then(r=>r.json()).then(data=>{"
        "let table=document.getElementById('devices');"
        "table.innerHTML='<tr><th>Name</th><th>Type</th><th>Status</th><th>Action</th></tr>';"
        "data.devices.forEach(d=>{"
        "if(d.ieee && d.ieee.startsWith('ctrl_')){ return; }"
        "let status=d.online?'online':'offline';"
        "if(d.last_seen_s>0){status+=' ('+d.last_seen_s+'s)';}"
        "let action=(d.ieee && d.ieee.startsWith('ctrl_'))?'':'<button onclick=\"deleteDevice(\\''+d.ieee+'\\')\">Delete</button>';"
        "let row='<tr><td>'+d.name+'</td><td>'+d.type+'</td><td>'+status+'</td><td>'+action+'</td></tr>';"
        "table.innerHTML+=row;"
        "});"
        "});"
        "}"

        /* auto refresh */

        "setInterval(loadDevices,3000);"
        "loadDevices();"

        "</script>"

        "</body>"
        "</html>";

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* =========================
   WEBSOCKET
   ========================= */

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {

        ws_client_fd = httpd_req_to_sockfd(req);

        ESP_LOGI(TAG, "WebSocket client connected");

        web_log_send("WebSocket connected");

        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT
    };

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read WebSocket frame length");
        return err;
    }

    if (frame.len == 0)
        return ESP_OK;

    uint8_t payload[32];
    if (frame.len >= sizeof(payload))
    {
        ESP_LOGW(TAG, "Ignoring oversized WebSocket command");
        return ESP_OK;
    }

    frame.payload = payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read WebSocket frame");
        return err;
    }

    payload[frame.len] = '\0';

    if (strcmp((char *)payload, PAIR_CMD) == 0)
    {
        if (!zigbee_core_start_pairing())
        {
            web_log_send("Pair mode already active or unavailable");
        }
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unknown WebSocket command: %s", (char *)payload);

    return ESP_OK;
}

/* =========================
   DEVICE LIST
   ========================= */
static esp_err_t devices_handler(httpd_req_t *req)
{
    char *json = malloc(4096);
    int offset = 0;
    int count;

    if (json == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Out of memory");
        return ESP_OK;
    }

    offset += snprintf(json + offset, 4096 - offset, "{ \"devices\": [");
    count = device_manager_count();

    for (int i = 0; i < count && offset < 3900; i++) {
        device_t *device = device_manager_get(i);
        bool online;
        uint32_t last_seen_s;

        if (device == NULL) {
            continue;
        }

        online = zigbee_core_is_device_online(device->ieee);
        last_seen_s = zigbee_core_device_last_seen_seconds(device->ieee);

        offset += snprintf(json + offset,
                           4096 - offset,
                           "{ \"name\":\"%s\", \"type\":\"%s\", \"ieee\":\"%s\", \"manufacturer\":\"%s\", \"model\":\"%s\", \"features\":\"%s\", \"endpoint\":%u, \"short_addr\":%u, \"device_id\":%u, \"online\":%s, \"last_seen_s\":%lu }%s",
                           device->name,
                           device->type,
                           device->ieee,
                           device->manufacturer,
                           device->model,
                           device->features,
                           device->endpoint,
                           device->short_addr,
                           device->device_id,
                           online ? "true" : "false",
                           (unsigned long)last_seen_s,
                           (i < count - 1) ? "," : "");
    }

    snprintf(json + offset, 4096 - offset, "] }");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);

    return ESP_OK;
}

static esp_err_t device_delete_handler(httpd_req_t *req)
{
    char query[96];
    char ieee[32];
    int index;
    device_t *device;
    int query_len = httpd_req_get_url_query_len(req);

    if (query_len <= 0 || query_len >= (int)sizeof(query)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Missing query");
        return ESP_OK;
    }

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "ieee", ieee, sizeof(ieee)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Missing ieee");
        return ESP_OK;
    }

    index = device_manager_find_by_ieee(ieee);
    device = (index >= 0) ? device_manager_get(index) : NULL;
    if (device == NULL) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "Device not found");
        return ESP_OK;
    }

    if (device_manager_is_control(device)) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Default devices cannot be deleted");
        return ESP_OK;
    }

    if (!device_manager_remove_by_ieee(ieee)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Delete failed");
        return ESP_OK;
    }

    {
        char json[1024];

        device_manager_get_json(json, sizeof(json));
        mqtt_publish_devices(json);
    }

    web_log_send("Device deleted");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* =========================
   SERVER START
   ========================= */

void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK)
    {

        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler};

        httpd_uri_t ws = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler,
            .is_websocket = true};

        httpd_uri_t devices = {
            .uri = "/devices",
            .method = HTTP_GET,
            .handler = devices_handler};

        httpd_uri_t device_delete = {
            .uri = "/device/delete",
            .method = HTTP_POST,
            .handler = device_delete_handler};

        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &ws);
        httpd_register_uri_handler(server, &devices);
        httpd_register_uri_handler(server, &device_delete);

        ESP_LOGI(TAG, "Web server started");

        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
        {

            ESP_LOGI(TAG, "Dashboard: http://" IPSTR,
                     IP2STR(&ip_info.ip));
        }
    }
}
