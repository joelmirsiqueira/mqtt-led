/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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

static const char *TAG = "MQTT_APP";
static esp_mqtt_client_handle_t g_client = NULL;
static QueueHandle_t gpio_evt_queue = NULL;
static bool led_status = false;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

void led_inverte()
{
    led_status = !led_status;
    gpio_set_level(LED_PIN, led_status);

    const char *status = led_status ? "ON" : "OFF";
    esp_mqtt_client_publish(g_client, "ads/embarcado/atividade/led_status", status, 0, 1, 0);
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void button_task(void* arg)
{
    uint32_t io_num;
    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(io_num) == 0) {
                 if (g_client) {
                    int msg_id = esp_mqtt_client_publish(g_client, "ads/embarcado/atividade", "1", 0, 1, 0);
                    ESP_LOGI(TAG, "Mensagem '1' enviada, msg_id=%d", msg_id);
                } else {
                    ESP_LOGE(TAG, "Cliente MQTT não inicializado.");
                }
            }

            while(gpio_get_level(io_num) == 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
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
        msg_id = esp_mqtt_client_subscribe(client, "ads/embarcado/atividade", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        ESP_LOGI(TAG, "Inscrito com sucesso no tópico ads/embarcado/atividade");
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
        led_inverte();
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
        .session.last_will.topic = "/topic/will",
        .session.last_will.msg = "i will leave",
        .session.last_will.msg_len = 12,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    g_client = esp_mqtt_client_init(&mqtt5_cfg);
    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);
    esp_mqtt_client_start(g_client);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << BUTTON_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, gpio_isr_handler, (void*) BUTTON_PIN);

    mqtt_app_start();
}
