#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "RGB_LED";

//GPIO mapping from your wiring
#define LED_R GPIO_NUM_13
#define LED_G GPIO_NUM_12
#define LED_B GPIO_NUM_11

// 1 = common-cathode LED (HIGH turns channel on)
// 0 = common-anode LED (LOW turns channel on)
#define RGB_ACTIVE_HIGH 1

// Per your guidance: pin app logic to Core 1
#define RGB_TASK_CORE 1
#define RGB_TASK_STACK_WORDS 2048
#define RGB_TASK_PRIORITY 5

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    const char *name;
} rgb_color_t;

static inline int rgb_level(uint8_t on)
{
    return RGB_ACTIVE_HIGH ? (on ? 1 : 0) : (on ? 0 : 1);
}

static esp_err_t rgb_set(const rgb_color_t *c)
{
    esp_err_t err;

    err = gpio_set_level(LED_R, rgb_level(c->r));
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(LED_G, rgb_level(c->g));
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(LED_B, rgb_level(c->b));
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

static void rgb_task(void *arg)
{
    (void)arg;

    static const rgb_color_t colors[] = {
        {1, 1, 1, "white"},
        {1, 0, 0, "red"},
        {0, 1, 0, "green"},
        {0, 0, 1, "blue"},
        {1, 1, 0, "yellow"},
        {1, 0, 1, "magenta"},
        {0, 1, 1, "cyan"},
        {0, 0, 0, "off"}
    };

    const size_t color_count = sizeof(colors) / sizeof(colors[0]);

    while (1) {
        for (size_t i = 0; i < color_count; i++) {
            ESP_ERROR_CHECK(rgb_set(&colors[i]));
            ESP_LOGI(TAG, "Color -> %s", colors[i].name);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}
void app_main(void)
{
     gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_R) | (1ULL << LED_G) | (1ULL << LED_B),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    BaseType_t task_ok = xTaskCreatePinnedToCore(
        rgb_task,
        "rgb_task",
        RGB_TASK_STACK_WORDS,
        NULL,
        RGB_TASK_PRIORITY,
        NULL,
        RGB_TASK_CORE
    );

    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create rgb_task");
    }
}
