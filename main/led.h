// led.h
#pragma once
#include "driver/gpio.h"

typedef enum {
    LED_OFF,
    LED_RED,
    LED_GREEN,
    LED_BLUE
} led_color_t;

void led_init(void);
void led_set(led_color_t color);