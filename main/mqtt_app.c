#include "mqtt_app.h"
#include "nfc_scanner.h"
#include "led.h"
#include "pins.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static uint8_t s_flash_count = 3;

/* LWT payload must remain valid for the life of the client */
static char s_lwt[64];

bool mqtt_is_connected(void)
{
    return s_connected;
}

uint8_t mqtt_get_flash_count(void)
{
    return s_flash_count;
}

static void parse_cmd(const char *data, int len)
{
    char buf[256];
    if (len >= (int)sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, data, len);
    buf[len] = '\0';
    ESP_LOGI(TAG, "cmd: %s", buf);

    int flash = 3;
    char *fc = strstr(buf, "\"flashCount\"");
    if (fc) {
        char *colon = strchr(fc, ':');
        if (colon) {
            flash = atoi(colon + 1);
        }
    }
    if (flash < 1) {
        flash = 1;
    }
    if (flash > 10) {
        flash = 10;
    }
    s_flash_count = (uint8_t)flash;
    nfc_set_flash_count(s_flash_count);

    char *tt = strstr(buf, "\"targetTag\"");
    if (tt) {
        char *q1 = strchr(tt, ':');
        if (q1) {
            q1 = strchr(q1, '"');
            if (q1) {
                q1++;
                char *q2 = strchr(q1, '"');
                if (q2) {
                    char tag[16] = {0};
                    size_t n = (size_t)(q2 - q1);
                    if (n > 15) {
                        n = 15;
                    }
                    memcpy(tag, q1, n);
                    nfc_set_target_tag(tag);
                }
            }
        }
    }

    led_set_flash(s_flash_count, nfc_target_is_present());
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        s_connected = true;
        ESP_LOGI(TAG, "Connected to broker");

        char reg[64];
        snprintf(reg, sizeof(reg), "{\"deviceId\":\"%s\"}", DEVICE_ID);
        esp_mqtt_client_publish(s_client, "lab3/register", reg, 0, 1, 0);
        ESP_LOGI(TAG, "Published register: %s", reg);

        char topic[64];
        snprintf(topic, sizeof(topic), "lab3/%s/cmd", DEVICE_ID);
        esp_mqtt_client_subscribe(s_client, topic, 1);
        ESP_LOGI(TAG, "Subscribed %s", topic);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected");
        break;
    case MQTT_EVENT_DATA: {
        char topic[128] = {0};
        if (event->topic_len > 0 && event->topic_len < (int)sizeof(topic)) {
            memcpy(topic, event->topic, event->topic_len);
            topic[event->topic_len] = '\0';
        }
        char expect[64];
        snprintf(expect, sizeof(expect), "lab3/%s/cmd", DEVICE_ID);
        if (strcmp(topic, expect) == 0) {
            parse_cmd(event->data, event->data_len);
        }
        break;
    }
    default:
        break;
    }
}

void mqtt_publish_status(bool target_found)
{
    if (!s_client || !s_connected) {
        return;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "lab3/%s/status", DEVICE_ID);

    char body[48];
    snprintf(body, sizeof(body), "{\"targetFound\":%s}",
             target_found ? "true" : "false");

    esp_mqtt_client_publish(s_client, topic, body, 0, 1, 0);
    ESP_LOGI(TAG, "status %s", body);
}

void mqtt_app_start(void)
{
    snprintf(s_lwt, sizeof(s_lwt), "{\"deviceId\":\"%s\"}", DEVICE_ID);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .session.last_will = {
            .topic = "lab3/unregister",
            .msg = s_lwt,
            .msg_len = 0,   /* 0 = use strlen(msg) */
            .qos = 1,
            .retain = 0
        },
    };

    s_client = esp_mqtt_client_init(&cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                    mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
    ESP_LOGI(TAG, "MQTT starting → %s (LWT deviceId=%s)", MQTT_BROKER_URI, DEVICE_ID);
}