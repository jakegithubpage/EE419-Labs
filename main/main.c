#include "nvs_helpers.h"
#include "led.h"
#include "wifi_manager.h"
#include "nfc_scanner.h"
#include "webserver.h"
#include "mqtt_app.h"
#include "esp_log.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_helpers_init());
    led_init();

    nfc_scanner_start();
    wifi_manager_start();   // portal or STA; returns only when STA connected
    webserver_start();
    mqtt_app_start();

    ESP_LOGI("main", "Lab 3 ready");
}