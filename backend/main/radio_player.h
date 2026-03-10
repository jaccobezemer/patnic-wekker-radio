#pragma once

#include "esp_err.h"

typedef enum {
    RADIO_STATUS_STOPPED = 0x00,
    RADIO_STATUS_PLAYING = 0x01,
    RADIO_STATUS_PAUSED  = 0x02,
    RADIO_STATUS_ERROR   = 0xFF,
} radio_status_t;

/** Initialiseer de radio speler (ES8388 codec + I2S). */
esp_err_t radio_player_init(void);

/** Start het afspelen van een stream-URL (gestuurd door frontend). */
esp_err_t radio_player_play_url(const char *url);

/** Stop afspelen volledig. */
void radio_player_stop(void);

/** Pauzeer afspelen (stopt stream, URL blijft bewaard). */
void radio_player_pause(void);

/** Hervat afspelen na pauze. Als niet gepauzeerd: geen actie. */
void radio_player_resume(void);

/** Stel volume in (0-100). */
void radio_player_set_volume(int vol);

/** Geeft huidige afspeelstatus terug. */
radio_status_t radio_player_get_status(void);

/** Geeft huidig volume terug. */
int radio_player_get_volume(void);
