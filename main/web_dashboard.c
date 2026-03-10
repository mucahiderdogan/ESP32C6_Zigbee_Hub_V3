#include "web_dashboard.h"
#include "zigbee.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG="web";

static esp_err_t root_handler(httpd_req_t *req)
{
    const char *html=
    "<html><head><title>Zigbee Hub</title></head>"
    "<body>"
    "<h1>ESP32 Zigbee Hub</h1>"
    "<button onclick='pair()'>Enable Pairing</button>"
    "<div id=\"devices\"></div>"
    "<script>"
    "function load(){fetch('/api/devices').then(r=>r.json()).then(d=>{"
    "let html='';"
    "d.devices.forEach(dev=>{html+='<p>'+dev.short_addr+'</p>';});"
    "document.getElementById('devices').innerHTML=html;});}"
    "function pair(){fetch('/api/pair',{method:'POST'});}"
    "load();"
    "</script>"
    "</body></html>";

    httpd_resp_send(req,html,HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t devices_handler(httpd_req_t *req)
{
    char json[1024];
    zigbee_build_device_json(json,sizeof(json));

    httpd_resp_set_type(req,"application/json");
    httpd_resp_send(req,json,HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t pair_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req,"pairing enabled");
    return ESP_OK;
}

void web_dashboard_start()
{
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server=NULL;

    httpd_start(&server,&config);

    httpd_uri_t root={
        .uri="/",
        .method=HTTP_GET,
        .handler=root_handler
    };

    httpd_uri_t devices={
        .uri="/api/devices",
        .method=HTTP_GET,
        .handler=devices_handler
    };

    httpd_uri_t pair={
        .uri="/api/pair",
        .method=HTTP_POST,
        .handler=pair_handler
    };

    httpd_register_uri_handler(server,&root);
    httpd_register_uri_handler(server,&devices);
    httpd_register_uri_handler(server,&pair);

    ESP_LOGI(TAG,"Web dashboard started");
}
