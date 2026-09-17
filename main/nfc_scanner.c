#include "nfc_scanner.h"
#include "led.h"
#include "nvs_helpers.h"
#include "pins.h"
#include "pn532.h"
#include "pn532_driver_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "nfc";

uint8_t  g_current_uid[10] = {0};
uint8_t  g_current_uid_len = 0;
bool     g_tag_present = false;
bool     g_learning_mode = false;

static uint8_t target_uid[10];
static uint8_t target_uid_len = 0;
static bool    target_valid = false;

static void load_target(void)
{
    target_valid = false;
    target_uid_len = 0;

    if (nvs_load_target_uid(target_uid, &target_uid_len) == ESP_OK &&
        nvs_target_is_set()) {
        target_valid = true;
        ESP_LOGI(TAG, "Loaded existing target (%d bytes)", target_uid_len);
        ESP_LOG_BUFFER_HEX(TAG, target_uid, target_uid_len);
    } else {
        ESP_LOGI(TAG, "No target in NVS → learning mode");
    }
}

static bool uid_equal(const uint8_t *a, uint8_t alen,
                      const uint8_t *b, uint8_t blen)
{
    return (alen == blen) && (memcmp(a, b, alen) == 0);
}

static void nfc_task(void *arg)
{
    pn532_io_t io;
    ESP_ERROR_CHECK(pn532_new_driver_i2c(PN532_SDA_GPIO, PN532_SCL_GPIO,
                                         PN532_RESET_GPIO, PN532_IRQ_GPIO,
                                         I2C_NUM_0, &io));

    while (pn532_init(&io) != ESP_OK) {
        ESP_LOGW(TAG, "PN532 init failed, retry...");
        pn532_release(&io);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    uint32_t ver = 0;
    while (pn532_get_firmware_version(&io, &ver) != ESP_OK) {
        ESP_LOGW(TAG, "No PN532 found");
        pn532_reset(&io);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "PN532 firmware OK: 0x%08lx", (unsigned long)ver);

    pn532_set_passive_activation_retries(&io, 0xFF);

    // ---- Decide learning mode ----
    load_target();
    if (!target_valid) {
        g_learning_mode = true;
        led_set(LED_BLUE);
        ESP_LOGI(TAG, ">>> LEARNING MODE – present a tag to set as target");
    } else {
        g_learning_mode = false;
        led_set(LED_OFF);
        ESP_LOGI(TAG, ">>> NORMAL MODE – waiting for tags");
    }

    while (1) {
        uint8_t uid[10] = {0};
        uint8_t uid_len = 0;

        esp_err_t err = pn532_read_passive_target_id(
            &io, PN532_BRTY_ISO14443A_106KBPS, uid, &uid_len, 300);

        if (err == ESP_OK && uid_len > 0) {
            memcpy(g_current_uid, uid, uid_len);
            g_current_uid_len = uid_len;
            g_tag_present = true;

            ESP_LOGI(TAG, "Tag detected (%d bytes):", uid_len);
            ESP_LOG_BUFFER_HEX(TAG, uid, uid_len);

            if (g_learning_mode) {
                // First tag becomes the target
                nvs_save_target_uid(uid, uid_len);
                memcpy(target_uid, uid, uid_len);
                target_uid_len = uid_len;
                target_valid = true;
                g_learning_mode = false;

                ESP_LOGI(TAG, ">>> TARGET SAVED");
                led_set(LED_GREEN);
            }
            else if (target_valid &&
                     uid_equal(uid, uid_len, target_uid, target_uid_len)) {
                ESP_LOGI(TAG, ">>> MATCH – target tag");
                led_set(LED_GREEN);
            }
            else {
                ESP_LOGI(TAG, ">>> DIFFERENT tag");
                led_set(LED_RED);
            }
        }
        else {
            // No tag
            if (g_tag_present) {
                ESP_LOGI(TAG, "Tag removed");
            }
            g_tag_present = false;
            g_current_uid_len = 0;

            if (g_learning_mode) {
                led_set(LED_BLUE);
            } else {
                led_set(LED_OFF);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void nfc_scanner_start(void)
{
    xTaskCreate(nfc_task, "nfc_task", 4096, NULL, 5, NULL);
}