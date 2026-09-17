#include "nvs_helpers.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "pins.h"
#include <string.h>
#include "esp_log.h"

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

bool nvs_target_is_set(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;   // namespace does not exist yet → no target
    }
    uint8_t set = 0;
    nvs_get_u8(h, NVS_KEY_TARGET_SET, &set);
    nvs_close(h);
    return set != 0;
}

esp_err_t nvs_save_target_uid(const uint8_t *uid, uint8_t len)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_blob(h, NVS_KEY_TARGET_UID, uid, len));
    ESP_ERROR_CHECK(nvs_set_u8(h, NVS_KEY_TARGET_UID_LEN, len));
    ESP_ERROR_CHECK(nvs_set_u8(h, NVS_KEY_TARGET_SET, 1));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    ESP_LOGI(TAG, "Target UID saved (%d bytes)", len);
    return ESP_OK;
}

esp_err_t nvs_load_target_uid(uint8_t *uid, uint8_t *len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // ESP_ERR_NVS_NOT_FOUND is normal on first boot
        return err;
    }

    size_t required = 10;
    err = nvs_get_blob(h, NVS_KEY_TARGET_UID, uid, &required);
    if (err == ESP_OK) {
        uint8_t l = 0;
        nvs_get_u8(h, NVS_KEY_TARGET_UID_LEN, &l);
        *len = l;
    }
    nvs_close(h);
    return err;
}

esp_err_t nvs_clear_target(void)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    nvs_erase_key(h, NVS_KEY_TARGET_UID);
    nvs_erase_key(h, NVS_KEY_TARGET_UID_LEN);
    ESP_ERROR_CHECK(nvs_set_u8(h, NVS_KEY_TARGET_SET, 0));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    ESP_LOGI(TAG, "Target cleared");
    return ESP_OK;
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
    return ESP_OK;
}