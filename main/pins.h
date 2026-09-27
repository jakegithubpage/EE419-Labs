#pragma once

#define LED_R_GPIO       13
#define LED_G_GPIO       12
#define LED_B_GPIO       11

#define PN532_SDA_GPIO   3
#define PN532_SCL_GPIO   4
#define PN532_RESET_GPIO (-1)
#define PN532_IRQ_GPIO   (-1)

#define DEVICE_ID        "mcclurejr-esp32s3"
#define MDNS_HOSTNAME    DEVICE_ID

#define NVS_NAMESPACE        "lab3"
#define NVS_KEY_WIFI_SSID    "wifi_ssid"
#define NVS_KEY_WIFI_PASS    "wifi_pass"

#define MQTT_BROKER_URI  "mqtt://NEB426.local"