// webserver.c
#include "webserver.h"
#include "nfc_scanner.h"
#include "nvs_helpers.h"
#include "led.h"
#include "pins.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "mdns.h"
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "http";

static void uid_to_hex(const uint8_t *uid, uint8_t len, char *out, size_t out_len)
{
    if (len == 0) {
        snprintf(out, out_len, "(none)");
        return;
    }
    size_t pos = 0;
    for (int i = 0; i < len && pos + 3 < out_len; i++) {
        pos += snprintf(out + pos, out_len - pos, "%02X%s", uid[i], (i < len-1) ? ":" : "");
    }
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char target_hex[40] = "(not set)";
    char current_hex[40] = "(none)";
    uint8_t tuid[10]; uint8_t tlen = 0;
    if (nvs_load_target_uid(tuid, &tlen) == ESP_OK) {
        uid_to_hex(tuid, tlen, target_hex, sizeof(target_hex));
    }
    if (g_tag_present) {
        uid_to_hex(g_current_uid, g_current_uid_len, current_hex, sizeof(current_hex));
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &timeinfo);

    char html[1024];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>ESP32-S3 NFC</title>"
        "<meta http-equiv='refresh' content='3'></head><body>"
        "<h1>ESP32-S3 NFC Status</h1>"
        "<p><b>Date/Time:</b> %s</p>"
        "<p><b>Target NFC tag:</b> %s</p>"
        "<p><b>Current detected tag:</b> %s</p>"
        "<form method='POST' action='/reset'>"
        "<button type='submit'>Reset Target NFC Tag</button>"
        "</form></body></html>",
        timestr, target_hex, current_hex);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t reset_post_handler(httpd_req_t *req)
{
    nvs_clear_target();
    g_learning_mode = true;
    led_set(LED_BLUE);
    ESP_LOGI(TAG, "Target cleared – waiting for new tag");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void webserver_start(void)
{
    // SNTP for wall-clock time (optional but nice)
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // mDNS
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-S3 NFC Lab"));
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: http://%s.local", MDNS_HOSTNAME);

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_uri_t reset = { .uri = "/reset", .method = HTTP_POST, .handler = reset_post_handler };
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &reset);
        ESP_LOGI(TAG, "HTTP server started");
    }
}