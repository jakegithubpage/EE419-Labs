#pragma once

#include <stdbool.h>
#include <stdint.h>

void mqtt_app_start(void);
void mqtt_publish_status(bool target_found);
bool mqtt_is_connected(void);
uint8_t mqtt_get_flash_count(void);