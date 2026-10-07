#include "led.h"
#include "pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led";

#define LED_ACTIVE_HIGH 1

static volatile uint8_t s_flash_count = 3;
static volatile bool    s_target_present = false;
static TaskHandle_t     s_flash_task = NULL;

static void apply_color(led_color_t color)
{
#if LED_ACTIVE_HIGH
    gpio_set_level(LED_R_GPIO, color == LED_RED);
    gpio_set_level(LED_G_GPIO, color == LED_GREEN);
    gpio_set_level(LED_B_GPIO, color == LED_BLUE);
#else
    gpio_set_level(LED_R_GPIO, color != LED_RED);
    gpio_set_level(LED_G_GPIO, color != LED_GREEN);
    gpio_set_level(LED_B_GPIO, color != LED_BLUE);
#endif
}

void led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_R_GPIO) | (1ULL << LED_G_GPIO) | (1ULL << LED_B_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
    apply_color(LED_OFF);
    ESP_LOGI(TAG, "LED init R=%d G=%d B=%d", LED_R_GPIO, LED_G_GPIO, LED_B_GPIO);
}

void led_set(led_color_t color)
{
    apply_color(color);
}

static void flash_task(void *arg)
{
    while (1) {
        uint8_t n = s_flash_count;
        if (n < 1) n = 1;
        if (n > 10) n = 10;

        led_color_t c = s_target_present ? LED_GREEN : LED_RED;

        for (uint8_t i = 0; i < n; i++) {
            apply_color(c);
            vTaskDelay(pdMS_TO_TICKS(200));
            apply_color(LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        apply_color(LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void led_set_flash(uint8_t flash_count, bool target_present)
{
    if (flash_count < 1) flash_count = 1;
    if (flash_count > 10) flash_count = 10;
    s_flash_count = flash_count;
    s_target_present = target_present;

    if (s_flash_task == NULL) {
        xTaskCreatePinnedToCore(flash_task, "led_flash", 2048, NULL, 5, &s_flash_task, 1);
        ESP_LOGI(TAG, "Flash task started count=%u present=%d", flash_count, (int)target_present);
    }
}