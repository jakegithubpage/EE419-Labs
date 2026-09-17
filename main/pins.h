#pragma once

#define LED_R_GPIO      13
#define LED_G_GPIO      12
#define LED_B_GPIO      11

#define PN532_SDA_GPIO  3
#define PN532_SCL_GPIO  4
#define PN532_RESET_GPIO (-1)   // not connected
#define PN532_IRQ_GPIO   (-1)   // polling mode

#define MDNS_HOSTNAME   "mcclurejr-esp32s3"   // change to your userid

#define NVS_NAMESPACE           "lab2"
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_TARGET_UID      "target_uid"
#define NVS_KEY_TARGET_UID_LEN  "target_uid_len"
#define NVS_KEY_TARGET_SET      "target_set"