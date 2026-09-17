#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t nvs_helpers_init(void);

bool nvs_target_is_set(void);
esp_err_t nvs_save_target_uid(const uint8_t *uid, uint8_t len);
esp_err_t nvs_load_target_uid(uint8_t *uid, uint8_t *len);
esp_err_t nvs_clear_target(void);

esp_err_t nvs_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t nvs_save_wifi(const char *ssid, const char *pass);