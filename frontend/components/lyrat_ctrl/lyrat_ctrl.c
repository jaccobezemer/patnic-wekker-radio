/*****************************************************************************
 * | File         :   lyrat_ctrl.c
 * | Function     :   UART communicatieprotocol driver voor ESP32-LyraT v4.3
 * | Info         :
 * |   Framed UART-protocol (460800 8N1, instelbaar via menuconfig):
 * |     Zenden:   [LEN:1][PAYLOAD:LEN bytes]
 * |     Ontvangen: idem – verwerkt door rx_task (FreeRTOS)
 * ----------------
 * | Version      :   V1.0
 * | Date         :   2026-03-08
 *
 ******************************************************************************/

#include "lyrat_ctrl.h"

#include <string.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ── Interne constanten ─────────────────────────────────────────────────── */

static const char *TAG = "lyrat_ctrl";

#define RX_BUF_SIZE     512     /* UART ontvangst-ringbuffer */
#define TX_BUF_SIZE     0       /* Geen TX-ringbuffer nodig  */
#define MAX_PAYLOAD     255     /* Maximale payload per frame */
#define RX_TASK_STACK   3072
#define RX_TASK_PRIO    5

/* ── Module-status ──────────────────────────────────────────────────────── */

static lyrat_callbacks_t s_cbs = {0};
static bool              s_initialized = false;

/* ── Interne hulpfunctie: stuur een geframed bericht ────────────────────── */

/**
 * Bouwt een frame op als: [LEN][payload...] en schrijft het naar UART.
 * Maximale payload-lengte: MAX_PAYLOAD bytes.
 */
static void send_frame(const uint8_t *payload, uint8_t len)
{
    if (!s_initialized) return;

    uint8_t frame[1 + MAX_PAYLOAD];
    frame[0] = len;
    memcpy(&frame[1], payload, len);
    uart_write_bytes(LYRAT_UART_NUM, (const char *)frame, 1 + len);
}

/* ── Ontvangst-taak ─────────────────────────────────────────────────────── */

static void rx_task(void *arg)
{
    uint8_t rx_buf[RX_BUF_SIZE];

    while (1) {
        /* Wacht op het lengte-byte */
        uint8_t len_byte = 0;
        int n = uart_read_bytes(LYRAT_UART_NUM, &len_byte, 1, pdMS_TO_TICKS(100));
        if (n != 1 || len_byte == 0) continue;

        /* len_byte is uint8_t: max 255 = MAX_PAYLOAD, buffer altijd groot genoeg */
        uint8_t payload[MAX_PAYLOAD];
        int received = 0;
        /* Lees alle payload-bytes; geef meerdere pogingen als UART traag is */
        while (received < len_byte) {
            n = uart_read_bytes(LYRAT_UART_NUM,
                                payload + received,
                                len_byte - received,
                                pdMS_TO_TICKS(50));
            if (n <= 0) break;
            received += n;
        }

        if (received != len_byte) {
            ESP_LOGW(TAG, "Onvolledig frame: verwacht %d, ontvangen %d", len_byte, received);
            continue;
        }

        uint8_t opcode = payload[0];

        switch (opcode) {

        /* ── REPLY_STATUS  [0x06][status][volume] ───────────────────── */
        case REPLY_STATUS:
            if (len_byte >= 3 && s_cbs.on_status) {
                lyrat_status_t status = (lyrat_status_t)payload[1];
                uint8_t        volume = payload[2];
                ESP_LOGD(TAG, "STATUS: status=%d volume=%d", status, volume);
                s_cbs.on_status(status, volume);
            }
            break;

        /* ── REPLY_AUDIO_LEVEL  [0x08][peak_L][peak_R] ──────────────── */
        case REPLY_AUDIO_LEVEL:
            if (len_byte >= 3 && s_cbs.on_audio_level) {
                s_cbs.on_audio_level(payload[1], payload[2]);
            }
            break;

        /* ── REPLY_TIME_SYNC  [0x09][b3][b2][b1][b0] big-endian ─────── */
        case REPLY_TIME_SYNC:
            if (len_byte >= 5 && s_cbs.on_time_sync) {
                uint32_t epoch = ((uint32_t)payload[1] << 24) |
                                 ((uint32_t)payload[2] << 16) |
                                 ((uint32_t)payload[3] <<  8) |
                                 ((uint32_t)payload[4]);
                ESP_LOGI(TAG, "TIME_SYNC: epoch=%lu", (unsigned long)epoch);
                s_cbs.on_time_sync(epoch);
            }
            break;

        /* ── REPLY_WEATHER  [0x0C][json bytes...] ────────────────────── */
        case REPLY_WEATHER:
            if (len_byte >= 2 && s_cbs.on_weather) {
                char json[WEKKER_MAX_WEATHER_LEN + 1];
                uint8_t jlen = len_byte - 1;
                if (jlen > WEKKER_MAX_WEATHER_LEN) jlen = WEKKER_MAX_WEATHER_LEN;
                memcpy(json, &payload[1], jlen);
                json[jlen] = '\0';
                ESP_LOGI(TAG, "WEATHER: %s", json);
                s_cbs.on_weather(json);
            }
            break;

        default:
            ESP_LOGW(TAG, "Onbekend opcode: 0x%02X (len=%d)", opcode, len_byte);
            break;
        }

        (void)rx_buf; /* rx_buf niet gebruikt; enkel als stack-buffer gereserveerd */
    }
}

/* ── Initialisatie ──────────────────────────────────────────────────────── */

esp_err_t lyrat_ctrl_init(const lyrat_callbacks_t *cbs)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Al geïnitialiseerd");
        return ESP_OK;
    }

    if (cbs) {
        s_cbs = *cbs;
    }

    const uart_config_t uart_cfg = {
        .baud_rate  = LYRAT_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(LYRAT_UART_NUM, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(LYRAT_UART_NUM,
                                 LYRAT_UART_TX_PIN,
                                 LYRAT_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(LYRAT_UART_NUM,
                                        RX_BUF_SIZE * 2,
                                        TX_BUF_SIZE,
                                        0, NULL, 0));

    s_initialized = true;

    xTaskCreate(rx_task, "lyrat_rx", RX_TASK_STACK, NULL, RX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "Geïnitialiseerd – TX=GPIO%d RX=GPIO%d %d baud",
             LYRAT_UART_TX_PIN, LYRAT_UART_RX_PIN, LYRAT_UART_BAUDRATE);

    return ESP_OK;
}

/* ── Publieke commando-functies ─────────────────────────────────────────── */

void lyrat_play(void)
{
    uint8_t payload[] = { CMD_PLAY };
    send_frame(payload, sizeof(payload));
    ESP_LOGD(TAG, "→ CMD_PLAY");
}

void lyrat_stop(void)
{
    uint8_t payload[] = { CMD_STOP };
    send_frame(payload, sizeof(payload));
    ESP_LOGD(TAG, "→ CMD_STOP");
}

void lyrat_pause(void)
{
    uint8_t payload[] = { CMD_PAUSE };
    send_frame(payload, sizeof(payload));
    ESP_LOGD(TAG, "→ CMD_PAUSE");
}

void lyrat_set_volume(uint8_t volume)
{
    uint8_t payload[] = { CMD_VOLUME, volume };
    send_frame(payload, sizeof(payload));
    ESP_LOGI(TAG, "→ CMD_VOLUME %d", volume);
}

void lyrat_set_url(const char *url, bool auto_play)
{
    if (!url) return;

    size_t url_len = strlen(url);
    if (url_len > 253) {
        ESP_LOGW(TAG, "URL te lang (%d tekens, max 253) – afgekapt", (int)url_len);
        url_len = 253;
    }

    /* Payload: [CMD_SET_URL][url_len:uint8][url-bytes] */
    uint8_t payload[2 + 253];
    payload[0] = CMD_SET_URL;
    payload[1] = (uint8_t)url_len;
    memcpy(&payload[2], url, url_len);
    send_frame(payload, (uint8_t)(2 + url_len));
    ESP_LOGI(TAG, "→ CMD_SET_URL (%d bytes)", (int)url_len);

    if (auto_play) {
        lyrat_play();
    }
}

void lyrat_request_status(void)
{
    uint8_t payload[] = { CMD_STATUS };
    send_frame(payload, sizeof(payload));
    ESP_LOGD(TAG, "→ CMD_STATUS");
}

void lyrat_set_eq_band(uint8_t band, int8_t gain)
{
    if (band > 9) {
        ESP_LOGW(TAG, "Ongeldige EQ-band: %d (0-9 toegestaan)", band);
        return;
    }
    uint8_t payload[] = { CMD_EQ, band, (uint8_t)gain };
    send_frame(payload, sizeof(payload));
    ESP_LOGD(TAG, "→ CMD_EQ band=%d gain=%d", band, gain);
}

void lyrat_request_time(void)
{
    uint8_t payload[] = { CMD_REQUEST_TIME };
    send_frame(payload, sizeof(payload));
    ESP_LOGD(TAG, "→ CMD_REQUEST_TIME");
}

void lyrat_request_weather(void)
{
    uint8_t payload[] = { CMD_REQUEST_WEATHER };
    send_frame(payload, sizeof(payload));
    ESP_LOGD(TAG, "→ CMD_REQUEST_WEATHER");
}

