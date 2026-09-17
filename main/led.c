// led.c
#include "led.h"
#include "pins.h"

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
}

void led_set(led_color_t color)
{
    // Assumes active-high (common cathode). Invert if your LED is common-anode.
    gpio_set_level(LED_R_GPIO, color == LED_RED);
    gpio_set_level(LED_G_GPIO, color == LED_GREEN);
    gpio_set_level(LED_B_GPIO, color == LED_BLUE);
}