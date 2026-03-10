#include "web_server.h"
#include "device_manager.h"
#include "zigbee_core.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"

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
        ".container{display:flex;height:90vh}"
        ".devices{width:40%;border-right:1px solid #333;padding:20px}"
        ".logs{width:60%;padding:20px}"
        "button{width:200px;height:50px;font-size:18px;margin-bottom:20px}"
        "table{width:100%;border-collapse:collapse}"
        "th,td{border-bottom:1px solid #333;padding:10px;text-align:left}"
        "#log{background:#000;color:#0f0;height:80%;overflow:auto;padding:10px}"
        "</style>"
        "</head>"

        "<body>"

        "<header>"
        "<h1>ESP32 Zigbee Gateway</h1>"
        "<button onclick=\"sendPairCommand()\">Pair Device (60s)</button>"
        "<button onclick=\"fetch('/reset')\">Reset Gateway</button>"
        "</header>"

        "<div class='container'>"

        "<div class='devices'>"
        "<h2>Devices</h2>"
        "<table id='devices'>"
        "<tr><th>Name</th><th>Type</th></tr>"
        "</table>"
        "</div>"

        "<div class='logs'>"
        "<h2>Logs</h2>"
        "<div id='log'></div>"
        "</div>"

        "</div>"

        "<script>"

        /* websocket */

        "let ws = new WebSocket('ws://' + location.host + '/ws');"

        "ws.onmessage = function(e){"
        "let log=document.getElementById('log');"
        "log.innerHTML += e.data + '<br>';"
        "log.scrollTop = log.scrollHeight;"
        "};"

        "function sendPairCommand(){"
        "if(ws.readyState===WebSocket.OPEN){"
        "ws.send('pair');"
        "}"
        "}"

        /* load devices */

        "function loadDevices(){"
        "fetch('/devices').then(r=>r.json()).then(data=>{"
        "let table=document.getElementById('devices');"
        "table.innerHTML='<tr><th>Name</th><th>Type</th></tr>';"
        "data.devices.forEach(d=>{"
        "let row='<tr><td>'+d.name+'</td><td>'+d.type+'</td></tr>';"
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
    char json[1024];

    device_manager_get_json(json, sizeof(json));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "Restarting gateway...");

    web_log_send("Gateway restarting");

    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_restart();

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

        httpd_uri_t reset = {
            .uri = "/reset",
            .method = HTTP_GET,
            .handler = reset_handler};

        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &ws);
        httpd_register_uri_handler(server, &devices);
        httpd_register_uri_handler(server, &reset);

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
