#include "nvs_helpers.h"
#include "led.h"
#include "wifi_manager.h"
#include "nfc_scanner.h"
#include "webserver.h"
#include "esp_log.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_helpers_init());
    led_init();

    // Start NFC early so learning mode can run even before Wi-Fi
    nfc_scanner_start();

    wifi_manager_start();   // blocks briefly until connected (or portal)
    webserver_start();

    ESP_LOGI("main", "System ready");
}