/*****************************************************************************
 * | File         :   tsl2591.h
 * | Function     :   TSL2591 digitale lichtsensor driver (I2C 0x29)
 * | Info         :
 * |   Meet full-spectrum (zichtbaar + IR) en IR-kanaal apart.
 * |   Berekent lux op basis van gain en integratietime-instelling.
 * |   Aangesloten op de gedeelde I2C-bus (GPIO 8/9) van de Waveshare.
 * ----------------
 * | Version      :   V1.0
 * | Date         :   2026-03-08
 *
 ******************************************************************************/

#ifndef __TSL2591_H
#define __TSL2591_H

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* ── Sensor I2C-adres ───────────────────────────────────────────────────── */
#define TSL2591_I2C_ADDR    0x29

/* ── Gain-opties ────────────────────────────────────────────────────────── */
typedef enum {
    TSL2591_GAIN_LOW    = 0x00,  /*   1× – fel licht / buiten         */
    TSL2591_GAIN_MED    = 0x10,  /*  25× – normaal binnenshuis        */
    TSL2591_GAIN_HIGH   = 0x20,  /* 428× – schemerig / nacht          */
    TSL2591_GAIN_MAX    = 0x30,  /* 9876× – zeer donker               */
} tsl2591_gain_t;

/* ── Integratietime-opties ──────────────────────────────────────────────── */
typedef enum {
    TSL2591_ITIME_100MS = 0x00,  /* 100 ms */
    TSL2591_ITIME_200MS = 0x01,  /* 200 ms */
    TSL2591_ITIME_300MS = 0x02,  /* 300 ms */
    TSL2591_ITIME_400MS = 0x03,  /* 400 ms */
    TSL2591_ITIME_500MS = 0x04,  /* 500 ms */
    TSL2591_ITIME_600MS = 0x05,  /* 600 ms */
} tsl2591_itime_t;

/* ── Publieke API ────────────────────────────────────────────────────────── */

/**
 * @brief Initialiseer de TSL2591 en voeg het toe aan de gedeelde I2C-bus.
 *
 * @param bus    Gedeelde I2C master bus handle (van DEV_I2C_Get_Bus()).
 * @param gain   Versterkingsfactor.
 * @param itime  Integratietime.
 * @return ESP_OK bij succes; ESP_ERR_NOT_FOUND als sensor niet reageert.
 */
esp_err_t tsl2591_init(i2c_master_bus_handle_t bus,
                        tsl2591_gain_t gain,
                        tsl2591_itime_t itime);

/**
 * @brief Lees de berekende verlichtingssterkte.
 *
 * @param lux  Uitvoerpointer voor de lux-waarde (≥ 0).
 *             Retourneert 0.0 bij overbelichting of sensor-fout.
 * @return ESP_OK bij succes.
 */
esp_err_t tsl2591_read_lux(float *lux);

/**
 * @brief Controleer of de sensor aanwezig en bereikbaar is.
 * @return true als device ID klopt (0x50).
 */
bool tsl2591_probe(void);

#endif /* __TSL2591_H */
