#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define LED_PIN GPIO_NUM_23
#define BUTTON_PIN GPIO_NUM_5
#define TOPIC_SYSTEM_STATUS "ads/embarcado/atividade/system_status"
#define TOPIC_LED_STATUS "ads/embarcado/atividade/led_status"
#define TOPIC_BUTTON_STATUS "ads/embarcado/atividade/button_status"

static const char *TAG = "MQTT_APP";
static esp_mqtt_client_handle_t g_client = NULL;
static QueueHandle_t gpio_evt_queue = NULL;

void led_set_state(int state)
{
    gpio_set_level(LED_PIN, state);
    esp_mqtt_client_publish(g_client, TOPIC_LED_STATUS, state ? "ON" : "OFF", 0, 1, 0);
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    ESP_LOGD(TAG, "free heap size is %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, TOPIC_SYSTEM_STATUS, "ONLINE", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        msg_id = esp_mqtt_client_subscribe(client, TOPIC_BUTTON_STATUS, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        msg_id = esp_mqtt_client_publish(client, TOPIC_SYSTEM_STATUS, "OFFLINE", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);
        if (strncmp(event->topic, TOPIC_BUTTON_STATUS, event->topic_len) == 0) {
            if (strncmp(event->data, "0", event->data_len) == 0) {
                led_set_state(1);
            } else if (strncmp(event->data, "1", event->data_len) == 0) {
                led_set_state(0);
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt5_cfg = {
        // .broker.address.uri = CONFIG_BROKER_URL,
        .broker.address.uri = "mqtt://test.mosquitto.org:1883",
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = true,
        .session.last_will.topic = TOPIC_SYSTEM_STATUS,
        .session.last_will.msg = "OFFLINE",
        .session.last_will.msg_len = 12,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    g_client = esp_mqtt_client_init(&mqtt5_cfg);
    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);
    esp_mqtt_client_start(g_client);
}

void wifi_connect()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
}

void button_handler()
{
    while (1) {
        if (gpio_get_level(BUTTON_PIN) == 0) {
            esp_mqtt_client_publish(g_client, TOPIC_BUTTON_STATUS, "0", 0, 1, 0);
            while (gpio_get_level(BUTTON_PIN) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            esp_mqtt_client_publish(g_client, TOPIC_BUTTON_STATUS, "1", 0, 1, 0);
            while (gpio_get_level(BUTTON_PIN) == 1)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    wifi_connect();

    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_PIN);

    mqtt_app_start();

    xTaskCreate(button_handler, "button_handler", 4096, NULL, 10, NULL);
}
