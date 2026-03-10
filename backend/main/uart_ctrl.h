#pragma once

#include "esp_err.h"
#include "wekker_protocol.h"
#include <stdint.h>

/**
 * Callbacks die uart_ctrl aanroept bij ontvangen commando's.
 * Velden mogen NULL zijn als een commando niet geïmplementeerd is.
 */
typedef struct {
    void (*on_play)(void);
    void (*on_stop)(void);
    void (*on_pause)(void);
    void (*on_volume)(uint8_t vol);
    void (*on_set_url)(const char *url);
    void (*on_status_request)(void);
    void (*on_eq)(uint8_t band, int8_t gain);
    void (*on_time_request)(void);
    void (*on_weather_request)(void);
} uart_ctrl_callbacks_t;

/** Initialiseer UART en start de ontvangst-taak. */
esp_err_t uart_ctrl_init(const uart_ctrl_callbacks_t *callbacks);

/** Stuur REPLY_STATUS naar de frontend. */
void uart_ctrl_send_status(uint8_t status, uint8_t volume);

/** Stuur REPLY_TIME_SYNC (UTC epoch als big-endian uint32). */
void uart_ctrl_send_time(uint32_t epoch);

/** Stuur REPLY_WEATHER (JSON string, max WEKKER_MAX_WEATHER_LEN bytes). */
void uart_ctrl_send_weather(const char *json);
