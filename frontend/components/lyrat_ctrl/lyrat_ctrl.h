#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "wekker_protocol.h"

/* ── Hardware configuratie ──────────────────────────────────────────────── */

#define LYRAT_UART_NUM      UART_NUM_2
#define LYRAT_UART_TX_PIN   CONFIG_LYRAT_UART_TX_PIN    /* Waveshare GPIO 15 → LyraT RX */
#define LYRAT_UART_RX_PIN   CONFIG_LYRAT_UART_RX_PIN    /* Waveshare GPIO 16 ← LyraT TX */
#define LYRAT_UART_BAUDRATE CONFIG_LYRAT_UART_BAUDRATE

/* ── Status type ─────────────────────────────────────────────────────────── */

typedef enum {
    LYRAT_STOPPED = WEKKER_STATUS_STOPPED,
    LYRAT_PLAYING = WEKKER_STATUS_PLAYING,
    LYRAT_PAUSED  = WEKKER_STATUS_PAUSED,
    LYRAT_ERROR   = WEKKER_STATUS_ERROR,
} lyrat_status_t;

/* ── Callback typen ──────────────────────────────────────────────────────── */

typedef void (*lyrat_status_cb_t)(lyrat_status_t status, uint8_t volume);
typedef void (*lyrat_audio_level_cb_t)(uint8_t peak_l, uint8_t peak_r);
typedef void (*lyrat_time_sync_cb_t)(uint32_t utc_epoch);

/**
 * Weerdata callback: json is een null-terminated JSON string,
 * bijv. {"temp":12.5,"hum":82}
 */
typedef void (*lyrat_weather_cb_t)(const char *json);

typedef struct {
    lyrat_status_cb_t      on_status;
    lyrat_audio_level_cb_t on_audio_level;
    lyrat_time_sync_cb_t   on_time_sync;
    lyrat_weather_cb_t     on_weather;
} lyrat_callbacks_t;

/* ── Publieke API ────────────────────────────────────────────────────────── */

esp_err_t lyrat_ctrl_init(const lyrat_callbacks_t *cbs);

void lyrat_play(void);
void lyrat_stop(void);
void lyrat_pause(void);
void lyrat_set_volume(uint8_t volume);
void lyrat_set_url(const char *url, bool auto_play);
void lyrat_request_status(void);
void lyrat_set_eq_band(uint8_t band, int8_t gain);
void lyrat_request_time(void);
void lyrat_request_weather(void);
