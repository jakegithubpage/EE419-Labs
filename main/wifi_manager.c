// wifi_manager.c  (simplified – expand portal from your Lab 1)
#include "wifi_manager.h"
#include "nvs_helpers.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi";
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_manager_start(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &inst_got_ip));

    char ssid[33] = {0}, pass[65] = {0};
    if (nvs_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK && strlen(ssid) > 0) {
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "Connecting to saved SSID: %s", ssid);
    } else {
        // Fallback: start SoftAP + captive portal (implement from Lab 1)
        // For brevity here we just log; replace with your Lab-1 portal code
        // that calls nvs_save_wifi() then restarts.
        ESP_LOGW(TAG, "No Wi-Fi credentials – start your captive portal here");
        // ... softAP + HTTP form that saves to NVS and reboots ...
    }

    // Wait up to 15 s for connection (optional)
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, pdMS_TO_TICKS(15000));
}