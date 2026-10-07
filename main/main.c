#include "nvs_helpers.h"
#include "led.h"
#include "wifi_manager.h"
#include "nfc_scanner.h"
#include "webserver.h"
#include "mqtt_app.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

static const char *TAG = "main";

static void wait_for_time_sync(void)
{
    ESP_LOGI(TAG, "Starting SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int max_retry = 30;

    while (timeinfo.tm_year < (2020 - 1900) && retry < max_retry) {
        ESP_LOGI(TAG, "Waiting for time sync... (%d/%d)", retry + 1, max_retry);
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now);
        localtime_r(&now, &timeinfo);
        retry++;
    }

    if (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGW(TAG, "Time sync failed – TLS to AWS may fail");
    } else {
        ESP_LOGI(TAG, "Time synced: %s", asctime(&timeinfo));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_helpers_init());
    led_init();

    nfc_scanner_start();
    wifi_manager_start();   /* blocks until STA connected (or stays in portal) */

    wait_for_time_sync();   /* required before AWS IoT TLS */

    webserver_start();
    mqtt_app_start();

    ESP_LOGI(TAG, "Lab ready");
}