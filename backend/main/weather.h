#pragma once
#include "esp_err.h"

/**
 * Start de periodieke weer-fetch taak (elke 10 minuten).
 * Haalt temperatuur en luchtvochtigheid op via Open-Meteo (geen API key nodig).
 * Roept uart_ctrl_send_weather() aan zodra verse data beschikbaar is.
 */
esp_err_t weather_init(void);

/**
 * Stuur direct de meest recente gecachede weerdata via UART.
 * Bedoeld als reactie op CMD_REQUEST_WEATHER.
 * Als er nog geen data is, wordt niets verstuurd.
 */
void weather_send_cached(void);
