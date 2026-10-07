#include "wifi_manager.h"
#include "nvs_helpers.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY 8
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connect (%d/%d)", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "GOT IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static const char *PORTAL_HTML =
    "<!DOCTYPE html><html><head><title>WiFi Setup</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:Arial;margin:40px;}input,button{font-size:1.1em;padding:10px;margin:8px 0;width:100%;max-width:320px;box-sizing:border-box;}"
    "button{background:#0d6efd;color:#fff;border:none;border-radius:6px;}</style></head><body>"
    "<h2>ESP32-S3 WiFi Setup</h2>"
    "<form method='POST' action='/save'>"
    "SSID<br><input name='ssid' required><br>"
    "Password<br><input name='pass' type='password'><br>"
    "<button type='submit'>Save &amp; Connect</button></form></body></html>";

static esp_err_t portal_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t portal_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t portal_save_handler(httpd_req_t *req)
{
    char buf[300] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "read error");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};
    char *p = strstr(buf, "ssid=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        if (end) *end = '\0';
        for (char *c = p; *c; c++) if (*c == '+') *c = ' ';
        strncpy(ssid, p, sizeof(ssid) - 1);
        if (end) *end = '&';
    }
    p = strstr(buf, "pass=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        if (end) *end = '\0';
        for (char *c = p; *c; c++) if (*c == '+') *c = ' ';
        strncpy(pass, p, sizeof(pass) - 1);
    }

    ESP_LOGI(TAG, "Saving SSID='%s'", ssid);
    nvs_save_wifi(ssid, pass);

    const char *ok = "<html><body><h2>Saved! Rebooting...</h2></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ok, HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
    return ESP_OK;
}

static void dns_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { vTaskDelete(NULL); return; }
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    uint8_t buf[512];
    while (1) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
        if (len < 12) continue;
        buf[2] = 0x81; buf[3] = 0x80; buf[7] = 1;
        int pos = len;
        buf[pos++] = 0xc0; buf[pos++] = 0x0c;
        buf[pos++] = 0x00; buf[pos++] = 0x01;
        buf[pos++] = 0x00; buf[pos++] = 0x01;
        buf[pos++] = 0x00; buf[pos++] = 0x00;
        buf[pos++] = 0x00; buf[pos++] = 0x1e;
        buf[pos++] = 0x00; buf[pos++] = 0x04;
        buf[pos++] = 192; buf[pos++] = 168; buf[pos++] = 4; buf[pos++] = 1;
        sendto(sock, buf, pos, 0, (struct sockaddr *)&src, slen);
    }
}

static void start_captive_portal(void)
{
    ESP_LOGI(TAG, "Starting SoftAP portal (ESP32-S3-Setup)");
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap = {0};
    strcpy((char *)ap.ap.ssid, "ESP32-S3-Setup");
    ap.ap.ssid_len = strlen("ESP32-S3-Setup");
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(dns_server_task, "dns", 4096, NULL, 5, NULL);

    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 16;
    cfg.lru_purge_enable = true;

    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Portal HTTP failed");
        return;
    }
    ESP_LOGI(TAG, "Portal at http://192.168.4.1");

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = portal_get_handler };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = portal_save_handler };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);

    const char *paths[] = {
        "/generate_204", "/gen_204", "/hotspot-detect.html",
        "/library/test/success.html", "/ncsi.txt", "/connecttest.txt",
        "/success.txt", "/canonical.html", NULL
    };
    for (int i = 0; paths[i]; i++) {
        httpd_uri_t u = { .uri = paths[i], .method = HTTP_GET, .handler = portal_redirect_handler };
        httpd_register_uri_handler(server, &u);
    }
    ESP_LOGI(TAG, "Connect to WiFi 'ESP32-S3-Setup' then open http://192.168.4.1");
}

void wifi_manager_start(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t h1, h2;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &h1));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &h2));

    char ssid[33] = {0};
    char pass[65] = {0};

    if (nvs_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK && strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Trying saved SSID: %s", ssid);
        wifi_config_t sta = {0};
        strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid) - 1);
        strncpy((char *)sta.sta.password, pass, sizeof(sta.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        ESP_ERROR_CHECK(esp_wifi_start());

        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Connected to AP");
            return;
        }
        ESP_LOGW(TAG, "Saved credentials failed");
    } else {
        ESP_LOGW(TAG, "No Wi-Fi credentials in NVS");
    }

    start_captive_portal();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}