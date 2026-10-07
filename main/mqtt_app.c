#include "mqtt_app.h"
#include "nfc_scanner.h"
#include "led.h"
#include "pins.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "mqtt";

/* x509 certificates stored externally (embedded via CMake) */
extern const uint8_t device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const uint8_t device_cert_pem_end[]   asm("_binary_device_cert_pem_end");
extern const uint8_t device_key_pem_start[]  asm("_binary_device_private_key_start");
extern const uint8_t device_key_pem_end[]    asm("_binary_device_private_key_end");
extern const uint8_t server_cert_pem_start[] asm("_binary_AmazonRootCA1_pem_start");
extern const uint8_t server_cert_pem_end[]   asm("_binary_AmazonRootCA1_pem_end");

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

        /* Subscribe first, then register */
        char topic[80];
        snprintf(topic, sizeof(topic), "ee419_lab/%s/cmd", DEVICE_ID);
        esp_mqtt_client_subscribe(s_client, topic, 1);
        ESP_LOGI(TAG, "Subscribed %s", topic);

        char reg[64];
        snprintf(reg, sizeof(reg), "{\"deviceId\":\"%s\"}", DEVICE_ID);
        esp_mqtt_client_publish(s_client, "ee419_lab/register", reg, 0, 1, 0);
        ESP_LOGI(TAG, "Published register: %s", reg);
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Subscribe confirmed msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle) {
            ESP_LOGE(TAG, "error_type=%d", event->error_handle->error_type);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "esp_tls_last_esp_err=0x%x esp_tls_stack_err=0x%x",
                         event->error_handle->esp_tls_last_esp_err,
                         event->error_handle->esp_tls_stack_err);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "connection_return_code=%d",
                         event->error_handle->connect_return_code);
            }
        }
        break;

    case MQTT_EVENT_DATA: {
        char topic[128] = {0};
        if (event->topic && event->topic_len > 0 &&
            event->topic_len < (int)sizeof(topic)) {
            memcpy(topic, event->topic, event->topic_len);
            topic[event->topic_len] = '\0';
        }

        ESP_LOGI(TAG, "MQTT data topic='%s' len=%d", topic, event->data_len);
        if (event->data && event->data_len > 0) {
            char body[256];
            int n = event->data_len < (int)sizeof(body) - 1
                        ? event->data_len
                        : (int)sizeof(body) - 1;
            memcpy(body, event->data, n);
            body[n] = '\0';
            ESP_LOGI(TAG, "payload: %s", body);
        }

        char expect[80];
        snprintf(expect, sizeof(expect), "ee419_lab/%s/cmd", DEVICE_ID);
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

    char topic[80];
    snprintf(topic, sizeof(topic), "ee419_lab/%s/status", DEVICE_ID);

    char body[48];
    snprintf(body, sizeof(body), "{\"targetFound\":%s}",
             target_found ? "true" : "false");

    esp_mqtt_client_publish(s_client, topic, body, 0, 1, 0);
    ESP_LOGI(TAG, "status %s -> %s", topic, body);
}

void mqtt_app_start(void)
{
    snprintf(s_lwt, sizeof(s_lwt), "{\"deviceId\":\"%s\"}", DEVICE_ID);

    static const char *alpn_protos[] = { "x-amzn-mqtt-ca", NULL };

    esp_mqtt_client_config_t mqtt_config = {
        .broker = {
            .address = {
                .hostname = CONFIG_MQTT_BROKER,
                .port = CONFIG_MQTT_PORT,
                .transport = MQTT_TRANSPORT_OVER_SSL,
            },
            .verification = {
                .certificate = (const char *)server_cert_pem_start,
                .certificate_len = 0,
                .alpn_protos = alpn_protos,
            },
        },
        .credentials = {
            .username = NULL,
            .client_id = DEVICE_ID,
            .set_null_client_id = false,
            .authentication = {
                .password = NULL,
                .certificate = (const char *)device_cert_pem_start,
                .certificate_len = 0,
                .key = (const char *)device_key_pem_start,
                .key_len = 0,
                .key_password = NULL,
                .key_password_len = 0,
                .use_secure_element = false,
                .ds_data = NULL,
            },
        },
        .session = {
            .last_will = {
                .topic = "ee419_lab/unregister",
                .msg = s_lwt,
                .msg_len = 0,
                .qos = 0,
                .retain = false,
            },
            .keepalive = 60,
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_config);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                    mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
    ESP_LOGI(TAG, "MQTT starting broker=%s:%d client_id=%s",
             CONFIG_MQTT_BROKER, CONFIG_MQTT_PORT, DEVICE_ID);
}