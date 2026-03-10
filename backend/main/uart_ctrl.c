#include "uart_ctrl.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "uart_ctrl";

#define MAX_URL_LEN     255
#define MAX_FRAME_LEN   255
#define UART_NUM        UART_NUM_2
#define UART_BAUD       CONFIG_UART_BAUDRATE
#define UART_RX_BUF     512

static uart_ctrl_callbacks_t s_cb;

/* ── Stuur een frame: [LEN:1][PAYLOAD:LEN] ──────────────────────── */
static void send_frame(const uint8_t *payload, uint8_t len)
{
    uart_write_bytes(UART_NUM, (const char *)&len, 1);
    uart_write_bytes(UART_NUM, (const char *)payload, len);
}

/* ── Publieke antwoord-functies ─────────────────────────────────── */

void uart_ctrl_send_status(uint8_t status, uint8_t volume)
{
    uint8_t payload[3] = { REPLY_STATUS, status, volume };
    send_frame(payload, sizeof(payload));
    ESP_LOGD(TAG, "STATUS_REPLY: status=0x%02X volume=%d", status, volume);
}

void uart_ctrl_send_time(uint32_t epoch)
{
    uint8_t payload[5] = {
        REPLY_TIME_SYNC,
        (epoch >> 24) & 0xFF,
        (epoch >> 16) & 0xFF,
        (epoch >>  8) & 0xFF,
        epoch & 0xFF,
    };
    send_frame(payload, sizeof(payload));
    ESP_LOGD(TAG, "TIME_SYNC: epoch=%lu", (unsigned long)epoch);
}

void uart_ctrl_send_weather(const char *json)
{
    if (!json) return;
    size_t jlen = strlen(json);
    if (jlen > WEKKER_MAX_WEATHER_LEN - 1) {
        ESP_LOGW(TAG, "Weather JSON te lang (%d), afgekapt", (int)jlen);
        jlen = WEKKER_MAX_WEATHER_LEN - 1;
    }
    uint8_t payload[1 + WEKKER_MAX_WEATHER_LEN];
    payload[0] = REPLY_WEATHER;
    memcpy(&payload[1], json, jlen);
    send_frame(payload, (uint8_t)(1 + jlen));
    ESP_LOGD(TAG, "WEATHER: %s", json);
}

/* ── Ontvangst-taak ─────────────────────────────────────────────── */
static void uart_rx_task(void *pvParam)
{
    static uint8_t frame[MAX_FRAME_LEN];

    while (1) {
        /* Lees LEN byte */
        uint8_t len = 0;
        int r = uart_read_bytes(UART_NUM, &len, 1, pdMS_TO_TICKS(100));
        if (r <= 0 || len == 0) {
            continue;
        }

        /* Lees PAYLOAD */
        int got = uart_read_bytes(UART_NUM, frame, len, pdMS_TO_TICKS(200));
        if (got != (int)len) {
            ESP_LOGW(TAG, "Onvolledig frame: verwacht %d, ontvangen %d", len, got);
            continue;
        }

        uint8_t opcode = frame[0];
        ESP_LOGD(TAG, "Commando ontvangen: 0x%02X (len=%d)", opcode, len);

        switch (opcode) {
            case CMD_PLAY:
                if (s_cb.on_play) s_cb.on_play();
                break;

            case CMD_STOP:
                if (s_cb.on_stop) s_cb.on_stop();
                break;

            case CMD_PAUSE:
                if (s_cb.on_pause) s_cb.on_pause();
                break;

            case CMD_VOLUME:
                if (len >= 2 && s_cb.on_volume) {
                    s_cb.on_volume(frame[1]);
                }
                break;

            case CMD_SET_URL: {
                /* Payload: [0x05][url_len:uint8][url:url_len bytes] */
                if (len < 3) {
                    ESP_LOGW(TAG, "CMD_SET_URL: payload te kort");
                    break;
                }
                uint8_t url_len = frame[1];
                if ((int)len < (int)(2 + url_len)) {
                    ESP_LOGW(TAG, "CMD_SET_URL: URL afgebroken");
                    break;
                }
                char url[MAX_URL_LEN + 1];
                uint8_t copy_len = url_len;
                memcpy(url, &frame[2], copy_len);
                url[copy_len] = '\0';
                ESP_LOGI(TAG, "CMD_SET_URL: '%s'", url);
                if (s_cb.on_set_url) s_cb.on_set_url(url);
                break;
            }

            case CMD_STATUS:
                if (s_cb.on_status_request) s_cb.on_status_request();
                break;

            case CMD_EQ:
                if (len >= 3 && s_cb.on_eq) {
                    s_cb.on_eq(frame[1], (int8_t)frame[2]);
                }
                break;

            case CMD_REQUEST_TIME:
                if (s_cb.on_time_request) s_cb.on_time_request();
                break;

            case CMD_REQUEST_WEATHER:
                if (s_cb.on_weather_request) s_cb.on_weather_request();
                break;

            default:
                ESP_LOGW(TAG, "Onbekend opcode: 0x%02X", opcode);
                break;
        }
    }
}

// /* ── Heartbeat-taak: elke 5s een leesbaar bericht ─────────────── */
// static void uart_heartbeat_task(void *pvParam)
// {
//     int count = 0;
//     while (1) {
//         char msg[64];
//         int len = snprintf(msg, sizeof(msg), "Hello this is LyraT talking, I'm alive! tick=%d\r\n", count++);
//         uart_write_bytes(UART_NUM, msg, len);
//         vTaskDelay(pdMS_TO_TICKS(5000));
//     }
// }

// static void uart_debug_task(void *pvParam)
// {
//     uint8_t buf[128];
//     while (1) {
//         int len = uart_read_bytes(UART_NUM, buf, sizeof(buf) - 1, pdMS_TO_TICKS(50));
//         if (len > 0) {
//             buf[len] = '\0';
//             ESP_LOGI(TAG, "RAW RX (%d bytes): %s", len, buf);
//             // Ook als hex:
//             for (int i = 0; i < len; i++) {
//                 ESP_LOGI(TAG, "  [%d] = 0x%02X", i, buf[i]);
//             }
//         }
//     }
// }

/* ── Init ───────────────────────────────────────────────────────── */
esp_err_t uart_ctrl_init(const uart_ctrl_callbacks_t *callbacks)
{
    if (callbacks) {
        s_cb = *callbacks;
    }

    const uart_config_t uart_config = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_RX_BUF * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM,
                                 CONFIG_UART_TX_PIN,   /* TX: LyraT → test ESP32 */
                                 CONFIG_UART_RX_PIN,   /* RX: test ESP32 → LyraT */
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx", 4096, NULL, 4, NULL, 0);

    // xTaskCreatePinnedToCore(uart_heartbeat_task, "uart_hb", 2048, NULL, 3, NULL, 0);
    // xTaskCreatePinnedToCore(uart_debug_task, "uart_dbg", 4096, NULL, 4, NULL, 0);

    ESP_LOGI(TAG, "UART%d init: RX=GPIO%d TX=GPIO%d @ %d baud",
             UART_NUM, CONFIG_UART_RX_PIN, CONFIG_UART_TX_PIN, UART_BAUD);
    return ESP_OK;
}
