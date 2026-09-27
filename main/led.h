#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    LED_OFF = 0,
    LED_RED,
    LED_GREEN,
    LED_BLUE
} led_color_t;

void led_init(void);
void led_set(led_color_t color);

/** Flash flash_count times (200 ms on / 200 ms off), then 1 s off, repeat.
 *  Color: green if target_present, else red. */
void led_set_flash(uint8_t flash_count, bool target_present);