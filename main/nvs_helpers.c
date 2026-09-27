#include "nvs_helpers.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "pins.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nvs_helpers";

esp_err_t nvs_helpers_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t nvs_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return ESP_FAIL;
    }
    size_t s_len = ssid_len, p_len = pass_len;
    esp_err_t e1 = nvs_get_str(h, NVS_KEY_WIFI_SSID, ssid, &s_len);
    esp_err_t e2 = nvs_get_str(h, NVS_KEY_WIFI_PASS, pass, &p_len);
    nvs_close(h);
    return (e1 == ESP_OK && e2 == ESP_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_save_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_str(h, NVS_KEY_WIFI_SSID, ssid));
    ESP_ERROR_CHECK(nvs_set_str(h, NVS_KEY_WIFI_PASS, pass));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    ESP_LOGI(TAG, "WiFi credentials saved");
    return ESP_OK;
}