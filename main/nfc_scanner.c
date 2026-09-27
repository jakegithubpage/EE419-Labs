#include "nfc_scanner.h"
#include "led.h"
#include "mqtt_app.h"
#include "pins.h"
#include "pn532.h"
#include "pn532_driver_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "nfc";

static uint8_t target_uid[4] = {0};
static uint8_t target_len = 0;
static bool    target_valid = false;

static uint8_t current_uid[10] = {0};
static uint8_t current_len = 0;
static bool    tag_present = false;
static bool    last_target_found = false;

static uint8_t s_flash_count = 3;

void nfc_set_target_tag(const char *hex8)
{
    if (!hex8 || strlen(hex8) < 8) {
        target_valid = false;
        target_len = 0;
        ESP_LOGW(TAG, "Invalid targetTag");
        return;
    }
    for (int i = 0; i < 4; i++) {
        char byte_str[3] = { hex8[i * 2], hex8[i * 2 + 1], 0 };
        target_uid[i] = (uint8_t)strtoul(byte_str, NULL, 16);
    }
    target_len = 4;
    target_valid = true;
    ESP_LOGI(TAG, "Target set: %02X%02X%02X%02X",
             target_uid[0], target_uid[1], target_uid[2], target_uid[3]);
}

void nfc_set_flash_count(uint8_t n)
{
    if (n < 1) n = 1;
    if (n > 10) n = 10;
    s_flash_count = n;
    led_set_flash(s_flash_count, last_target_found);
}

bool nfc_target_is_present(void)
{
    return last_target_found;
}

static void uid_to_hex(const uint8_t *uid, uint8_t len, char *out, size_t out_len)
{
    if (len == 0) {
        snprintf(out, out_len, "(none)");
        return;
    }
    size_t pos = 0;
    for (int i = 0; i < len && pos + 3 < out_len; i++) {
        pos += snprintf(out + pos, out_len - pos, "%02X%s", uid[i], (i < len - 1) ? "" : "");
    }
}

void nfc_get_current_uid_hex(char *out, size_t out_len)
{
    if (!tag_present) {
        snprintf(out, out_len, "(none)");
        return;
    }
    uid_to_hex(current_uid, current_len, out, out_len);
}

void nfc_get_target_uid_hex(char *out, size_t out_len)
{
    if (!target_valid) {
        snprintf(out, out_len, "(not set)");
        return;
    }
    uid_to_hex(target_uid, target_len, out, out_len);
}

static bool uid_match(const uint8_t *uid, uint8_t len)
{
    return target_valid && len == target_len && memcmp(uid, target_uid, len) == 0;
}

static void nfc_task(void *arg)
{
    pn532_io_t io;
    ESP_ERROR_CHECK(pn532_new_driver_i2c(PN532_SDA_GPIO, PN532_SCL_GPIO,
                                         PN532_RESET_GPIO, PN532_IRQ_GPIO,
                                         I2C_NUM_0, &io));

    while (pn532_init(&io) != ESP_OK) {
        ESP_LOGW(TAG, "PN532 init failed, retry");
        pn532_release(&io);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    uint32_t ver = 0;
    while (pn532_get_firmware_version(&io, &ver) != ESP_OK) {
        ESP_LOGW(TAG, "No PN532");
        pn532_reset(&io);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "PN532 OK 0x%08lx", (unsigned long)ver);
    pn532_set_passive_activation_retries(&io, 0xFF);

    led_set_flash(s_flash_count, false);

    while (1) {
        uint8_t uid[10] = {0};
        uint8_t uid_len = 0;
        esp_err_t err = pn532_read_passive_target_id(
            &io, PN532_BRTY_ISO14443A_106KBPS, uid, &uid_len, 300);

        bool found_now = false;

        if (err == ESP_OK && uid_len > 0) {
            memcpy(current_uid, uid, uid_len);
            current_len = uid_len;
            tag_present = true;
            found_now = uid_match(uid, uid_len);
        } else {
            tag_present = false;
            current_len = 0;
            found_now = false;
        }

        if (found_now != last_target_found) {
            last_target_found = found_now;
            ESP_LOGI(TAG, "Target found -> %s", found_now ? "true" : "false");
            mqtt_publish_status(found_now);
            led_set_flash(s_flash_count, found_now);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void nfc_scanner_start(void)
{
    xTaskCreatePinnedToCore(nfc_task, "nfc_task", 4096, NULL, 5, NULL, 1);
}