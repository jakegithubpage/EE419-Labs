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

    // NFC runs independently – do not touch this
    nfc_scanner_start();

    // Blocks until either connected to Wi-Fi OR portal is running
    wifi_manager_start();

    // Only reached when we have a real STA IP
    webserver_start();

    ESP_LOGI("main", "System ready (Wi-Fi + mDNS + web server)");
}