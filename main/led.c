#include "led.h"
#include "pins.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "led";

// Change to 1 if your LED is common-anode (on when pin is LOW)
#define LED_ACTIVE_HIGH 1

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
    led_set(LED_OFF);
    ESP_LOGI(TAG, "LED init done (R=%d G=%d B=%d)", LED_R_GPIO, LED_G_GPIO, LED_B_GPIO);
}

void led_set(led_color_t color)
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

    const char *name = "OFF";
    if (color == LED_RED)   name = "RED";
    if (color == LED_GREEN) name = "GREEN";
    if (color == LED_BLUE)  name = "BLUE";
    ESP_LOGI(TAG, "LED -> %s", name);
}