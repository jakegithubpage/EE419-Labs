#pragma once

#include <stddef.h>
#include "esp_err.h"

esp_err_t nvs_helpers_init(void);
esp_err_t nvs_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t nvs_save_wifi(const char *ssid, const char *pass);