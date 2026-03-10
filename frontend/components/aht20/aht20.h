/*****************************************************************************
 * | File         :   aht20.h
 * | Function     :   AHT20 temperatuur- en vochtigheidssensor driver (I2C 0x38)
 * | Info         :
 * |   Aangesloten op de gedeelde I2C-bus (GPIO 8/9) van de Waveshare.
 * ----------------
 * | Version      :   V1.0
 * | Date         :   2026-03-08
 *
 ******************************************************************************/

#ifndef __AHT20_H
#define __AHT20_H

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* ── Sensor I2C-adres ───────────────────────────────────────────────────── */
#define AHT20_I2C_ADDR  0x38

/* ── Publieke API ────────────────────────────────────────────────────────── */

/**
 * @brief Initialiseer de AHT20 en voeg het toe aan de gedeelde I2C-bus.
 *
 * @param bus  Gedeelde I2C master bus handle (van DEV_I2C_Get_Bus()).
 * @return ESP_OK bij succes; ESP_ERR_NOT_FOUND als sensor niet reageert.
 */
esp_err_t aht20_init(i2c_master_bus_handle_t bus);

/**
 * @brief Start een meting en lees temperatuur en luchtvochtigheid.
 *
 * Blokkeert ca. 80 ms (meetduur sensor).
 *
 * @param temp_c   Uitvoerpointer voor temperatuur in °C.
 * @param hum_pct  Uitvoerpointer voor relatieve luchtvochtigheid in %.
 * @return ESP_OK bij succes; ESP_ERR_TIMEOUT als sensor bezet blijft.
 */
esp_err_t aht20_read(float *temp_c, float *hum_pct);

#endif /* __AHT20_H */
