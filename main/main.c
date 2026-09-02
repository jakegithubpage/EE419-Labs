#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "LAB1_RGB_WIFI";

#define LED_R GPIO_NUM_13
#define LED_G GPIO_NUM_12
#define LED_B GPIO_NUM_11
#define RGB_ACTIVE_HIGH 1

#define RGB_TASK_CORE 1
#define RGB_TASK_STACK_WORDS 2048
#define RGB_TASK_PRIORITY 5

#define DNS_TASK_CORE 0
#define DNS_TASK_STACK_WORDS 3072
#define DNS_TASK_PRIORITY 4

#define WIFI_MAX_RETRY 10

#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"

#define AP_SSID "ESP32S3-Setup"
#define AP_CHANNEL 1
#define AP_MAX_CONN 4

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    const char *name;
} rgb_color_t;

typedef struct {
    bool running;
    int sock;
    TaskHandle_t task_handle;
    uint32_t ap_ip_be; /* IPv4 in network byte order */
} dns_server_t;

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_ap_netif = NULL;
static httpd_handle_t s_httpd = NULL;
static dns_server_t s_dns = {0};
static int s_retry_num = 0;

/* ---------- RGB ---------- */

static inline int rgb_level(uint8_t on)
{
    return RGB_ACTIVE_HIGH ? (on ? 1 : 0) : (on ? 0 : 1);
}

static esp_err_t rgb_set(const rgb_color_t *c)
{
    esp_err_t err = gpio_set_level(LED_R, rgb_level(c->r));
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(LED_G, rgb_level(c->g));
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(LED_B, rgb_level(c->b));
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

static void rgb_task(void *arg)
{
    (void)arg;

    static const rgb_color_t colors[] = {
        {1, 1, 1, "white"},
        {1, 0, 0, "red"},
        {0, 1, 0, "green"},
        {0, 0, 1, "blue"},
        {1, 1, 0, "yellow"},
        {1, 0, 1, "magenta"},
        {0, 1, 1, "cyan"},
        {0, 0, 0, "off"},
    };

    const size_t count = sizeof(colors) / sizeof(colors[0]);

    while (1) {
        for (size_t i = 0; i < count; i++) {
            ESP_ERROR_CHECK(rgb_set(&colors[i]));
            ESP_LOGI(TAG, "RGB color: %s", colors[i].name);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

static void rgb_init_and_start(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_R) | (1ULL << LED_G) | (1ULL << LED_B),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    BaseType_t ok = xTaskCreatePinnedToCore(
        rgb_task,
        "rgb_task",
        RGB_TASK_STACK_WORDS,
        NULL,
        RGB_TASK_PRIORITY,
        NULL,
        RGB_TASK_CORE
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create rgb_task");
    }
}

/* ---------- NVS credentials ---------- */

static esp_err_t wifi_creds_save(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str ssid failed: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_set_str(nvs, NVS_KEY_PASS, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str pass failed: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_commit(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    nvs_close(nvs);
    return ESP_OK;
}
//Load Creds function
static esp_err_t wifi_creds_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t req_ssid = ssid_len;
    size_t req_pass = pass_len;

    err = nvs_get_str(nvs, NVS_KEY_SSID, ssid, &req_ssid);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }

    err = nvs_get_str(nvs, NVS_KEY_PASS, pass, &req_pass);
    nvs_close(nvs);
    return err;
}
/* ---------- URL/form helpers ---------- */

static int hex_to_int(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static void url_decode(char *dst, const char *src, size_t dst_len)
{
    size_t di = 0;

    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_len; si++) {
        if (src[si] == '+') {
            dst[di++] = ' ';
        } else if (src[si] == '%' &&
                   isxdigit((unsigned char)src[si + 1]) &&
                   isxdigit((unsigned char)src[si + 2])) {
            int hi = hex_to_int(src[si + 1]);
            int lo = hex_to_int(src[si + 2]);

            if (hi >= 0 && lo >= 0) {
                dst[di++] = (char)((hi << 4) | lo);
                si += 2;
            }
        } else {
            dst[di++] = src[si];
        }
    }

    dst[di] = '\0';
}

static bool get_form_value(const char *body, const char *key, char *out, size_t out_len)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "%s=", key);

    const char *start = strstr(body, pattern);
    if (!start) {
        return false;
    }

    start += strlen(pattern);
    const char *end = strchr(start, '&');
    size_t enc_len = end ? (size_t)(end - start) : strlen(start);

    char enc[128];
    if (enc_len >= sizeof(enc)) {
        return false;
    }

    memcpy(enc, start, enc_len);
    enc[enc_len] = '\0';

    url_decode(out, enc, out_len);
    return true;
}

/* ---------- Captive portal DNS ---------- */

static size_t dns_skip_name(const uint8_t *buf, size_t len, size_t off)
{
    while (off < len) {
        uint8_t label_len = buf[off];

        if (label_len == 0) {
            return off + 1;
        }

        if ((label_len & 0xC0) == 0xC0) {
            return off + 2;
        }

        off += (size_t)label_len + 1;
    }

    return len;
}

static ssize_t dns_build_a_reply(
    const uint8_t *query,
    size_t qlen,
    uint8_t *reply,
    size_t rlen,
    uint32_t ip_be
) {
    if (qlen < 12 || rlen < 32) {
        return -1;
    }

    memcpy(reply, query, qlen > rlen ? rlen : qlen);

    reply[2] = 0x81;
    reply[3] = 0x80;

    reply[4] = 0x00;
    reply[5] = 0x01;
    reply[6] = 0x00;
    reply[7] = 0x01;
    reply[8] = 0x00;
    reply[9] = 0x00;
    reply[10] = 0x00;
    reply[11] = 0x00;

    size_t qend = dns_skip_name(query, qlen, 12);
    if (qend + 4 > qlen) {
        return -1;
    }

    size_t resp_off = qend + 4;
    if (resp_off + 16 > rlen) {
        return -1;
    }

    reply[resp_off++] = 0xC0;
    reply[resp_off++] = 0x0C;

    reply[resp_off++] = 0x00;
    reply[resp_off++] = 0x01;

    reply[resp_off++] = 0x00;
    reply[resp_off++] = 0x01;

    reply[resp_off++] = 0x00;
    reply[resp_off++] = 0x00;
    reply[resp_off++] = 0x00;
    reply[resp_off++] = 0x1E;

    reply[resp_off++] = 0x00;
    reply[resp_off++] = 0x04;

    memcpy(&reply[resp_off], &ip_be, 4);
    resp_off += 4;

    return (ssize_t)resp_off;
}

static void dns_task(void *arg)
{
    dns_server_t *dns = (dns_server_t *)arg;
    uint8_t rx[512];
    uint8_t tx[512];

    while (dns->running) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        ssize_t n = recvfrom(
            dns->sock,
            rx,
            sizeof(rx),
            0,
            (struct sockaddr *)&from_addr,
            &from_len
        );
        if (n <= 0) {
            continue;
        }

        ssize_t rn = dns_build_a_reply(rx, (size_t)n, tx, sizeof(tx), dns->ap_ip_be);
        if (rn > 0) {
            sendto(dns->sock, tx, (size_t)rn, 0, (struct sockaddr *)&from_addr, from_len);
        }
    }

    vTaskDelete(NULL);
}

static esp_err_t dns_start(uint32_t ap_ip_be)
{
    if (s_dns.running) {
        return ESP_OK;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket create failed");
        return ESP_FAIL;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        return ESP_FAIL;
    }

    s_dns.running = true;
    s_dns.sock = sock;
    s_dns.ap_ip_be = ap_ip_be;

    BaseType_t ok = xTaskCreatePinnedToCore(
        dns_task,
        "dns_task",
        DNS_TASK_STACK_WORDS,
        &s_dns,
        DNS_TASK_PRIORITY,
        &s_dns.task_handle,
        DNS_TASK_CORE
    );

    if (ok != pdPASS) {
        close(sock);
        s_dns.running = false;
        s_dns.sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DNS captive responder started");
    return ESP_OK;
}

static void dns_stop(void)
{
    if (!s_dns.running) {
        return;
    }

    s_dns.running = false;

    if (s_dns.sock >= 0) {
        shutdown(s_dns.sock, 0);
        close(s_dns.sock);
        s_dns.sock = -1;
    }

    s_dns.task_handle = NULL;
    ESP_LOGI(TAG, "DNS captive responder stopped");
}

/* ---------- Captive portal HTTP ---------- */

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>"
        "<title>ESP32S3 Wi-Fi Setup</title></head><body>"
        "<h2>Configure Wi-Fi</h2>"
        "<p>Enter your Wi-Fi credentials (for your case, SSID NEB426).</p>"
        "<form method='POST' action='/save'>"
        "SSID:<br><input name='ssid' maxlength='32'/><br><br>"
        "Password:<br><input name='pass' type='password' maxlength='63'/><br><br>"
        "<button type='submit'>Save and Reboot</button>"
        "</form></body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 255) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body length");
        return ESP_FAIL;
    }

    char body[256];
    int received = 0;

    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        received += r;
    }
    body[received] = '\0';

    char ssid[33] = {0};
    char pass[64] = {0};

    if (!get_form_value(body, "ssid", ssid, sizeof(ssid))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }

    if (!get_form_value(body, "pass", pass, sizeof(pass))) {
        pass[0] = '\0';
    }

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID cannot be empty");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(wifi_creds_save(ssid, pass));
    ESP_LOGI(TAG, "Saved credentials for SSID: %s", ssid);

    static const char ok_page[] =
        "<!doctype html><html><body><h3>Saved.</h3><p>Device will restart now.</p></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ok_page, HTTPD_RESP_USE_STRLEN);

    xTaskCreatePinnedToCore(restart_task, "restart_task", 2048, NULL, 3, NULL, 0);
    return ESP_OK;
}

static esp_err_t captive_portal_start_http(void)
{
    if (s_httpd) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &root_uri));

    httpd_uri_t save_uri = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &save_uri));

    return ESP_OK;
}

static void captive_portal_stop(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    dns_stop();
}

/* ---------- Wi-Fi ---------- */

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retry to connect to AP (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_start_sta_and_wait(const char *ssid, const char *pass)
{
    ESP_LOGI(TAG, "Starting STA for SSID: %s", ssid);

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(30000)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to Wi-Fi");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "STA connect failed or timed out");
    return ESP_FAIL;
}

static esp_err_t wifi_start_captive_portal(void)
{
    ESP_LOGI(TAG, "Starting captive portal");

    wifi_config_t ap_cfg = {0};
    strncpy((char *)ap_cfg.ap.ssid, AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = strlen(AP_SSID);
    ap_cfg.ap.channel = AP_CHANNEL;
    ap_cfg.ap.max_connection = AP_MAX_CONN;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(s_ap_netif, &ip_info));

    ESP_LOGI(TAG, "AP started: %s, portal at http://" IPSTR, AP_SSID, IP2STR(&ip_info.ip));

    ESP_ERROR_CHECK(captive_portal_start_http());
    ESP_ERROR_CHECK(dns_start(ip_info.ip.addr));

    return ESP_OK;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }

    esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL)
    );
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL)
    );

    rgb_init_and_start();

    char ssid[33] = {0};
    char pass[64] = {0};

    if (wifi_creds_load(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK) {
        ESP_LOGI(TAG, "Credentials found in NVS");

        if (wifi_start_sta_and_wait(ssid, pass) == ESP_OK) {
            captive_portal_stop();
            while (1) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        ESP_LOGW(TAG, "Stored credentials failed, falling back to captive portal");
    } else {
        ESP_LOGI(TAG, "No Wi-Fi credentials in NVS, launching captive portal");
    }

    ESP_ERROR_CHECK(wifi_start_captive_portal());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}