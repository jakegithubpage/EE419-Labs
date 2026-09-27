#pragma once

#include <stdint.h>
#include <stdbool.h>

void nfc_scanner_start(void);

/** Set target from MQTT (8 hex digits, e.g. "A1B2C3D4"). */
void nfc_set_target_tag(const char *hex8);

bool nfc_target_is_present(void);

/** For web page */
void nfc_get_current_uid_hex(char *out, size_t out_len);
void nfc_get_target_uid_hex(char *out, size_t out_len);
void nfc_set_flash_count(uint8_t n);