#include "webserver.h"
#include "nfc_scanner.h"
#include "mqtt_app.h"
#include "pins.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mdns.h"
#include "esp_sntp.h"
#include "lwip/ip4_addr.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "http";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char target_hex[24], current_hex[24];
    nfc_get_target_uid_hex(target_hex, sizeof(target_hex));
    nfc_get_current_uid_hex(current_hex, sizeof(current_hex));

    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &ti);

    char ipstr[16] = "0.0.0.0";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            snprintf(ipstr, sizeof(ipstr), IPSTR, IP2STR(&ip.ip));
        }
    }

    char html[1600];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>ESP32-S3 Lab3</title>"
        "<meta http-equiv='refresh' content='3'>"
        "<style>body{font-family:Arial;margin:40px;background:#f5f5f5;}"
        ".box{background:#fff;padding:24px;border-radius:12px;display:inline-block;box-shadow:0 2px 8px rgba(0,0,0,.1);}"
        "p{font-size:1.1em;margin:10px 0;}</style></head><body><div class='box'>"
        "<h1>ESP32-S3 NFC + MQTT</h1>"
        "<p><b>Device ID:</b> %s</p>"
        "<p><b>IP:</b> %s</p>"
        "<p><b>Time:</b> %s</p>"
        "<p><b>MQTT:</b> %s</p>"
        "<p><b>Flash count:</b> %u</p>"
        "<p><b>Target tag:</b> %s</p>"
        "<p><b>Current tag:</b> %s</p>"
        "<p><b>Target found:</b> %s</p>"
        "</div></body></html>",
        DEVICE_ID,
        ipstr,
        timestr,
        mqtt_is_connected() ? "connected" : "disconnected",
        (unsigned)mqtt_get_flash_count(),
        target_hex,
        current_hex,
        nfc_target_is_present() ? "true" : "false");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void webserver_start(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-S3 Lab3"));
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS http://%s.local", MDNS_HOSTNAME);

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(server, &root);
        ESP_LOGI(TAG, "HTTP server started");
    }
}