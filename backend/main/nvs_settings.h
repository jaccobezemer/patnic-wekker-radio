#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Initialiseer de NVS flash partitie.
 * Wist de partitie automatisch als die corrupt is.
 */
esp_err_t nvs_settings_init(void);

/**
 * Laad WiFi credentials uit NVS.
 * Geeft ESP_ERR_NVS_NOT_FOUND terug als er nog geen credentials zijn.
 */
esp_err_t nvs_settings_get_wifi(char *ssid, size_t ssid_len,
                                char *password, size_t pass_len);

/**
 * Sla WiFi credentials op in NVS.
 */
esp_err_t nvs_settings_set_wifi(const char *ssid, const char *password);
