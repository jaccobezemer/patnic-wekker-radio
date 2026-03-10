/*****************************************************************************
 * | File         :   aht20.c
 * | Function     :   AHT20 temperatuur- en vochtigheidssensor driver (I2C 0x38)
 * ----------------
 * | Version      :   V1.0
 * | Date         :   2026-03-08
 *
 * Meetprotocol (AHT20 datasheet):
 *   1. Initialisatie:  schrijf [0xBE, 0x08, 0x00]  → wacht 10 ms
 *   2. Trigger meting: schrijf [0xAC, 0x33, 0x00]  → wacht 80 ms
 *   3. Lees 6 bytes:   [status][hum_h][hum_m][hum_l|temp_h][temp_m][temp_l]
 *      bit 7 van status = 1 → sensor nog bezig
 *
 ******************************************************************************/

#include "aht20.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "aht20";

/* ── Module-state ───────────────────────────────────────────────────────── */

static i2c_master_dev_handle_t s_dev = NULL;

/* ── AHT20 commando bytes ───────────────────────────────────────────────── */

static const uint8_t CMD_INIT[]    = { 0xBE, 0x08, 0x00 };
static const uint8_t CMD_TRIGGER[] = { 0xAC, 0x33, 0x00 };

/* ── Publieke functies ──────────────────────────────────────────────────── */

esp_err_t aht20_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AHT20_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev));

    /* Wacht op power-on tijd (≥ 20 ms na VDD stabiel) */
    vTaskDelay(pdMS_TO_TICKS(30));

    /* Stuur initialisatiecommando */
    esp_err_t ret = i2c_master_transmit(s_dev, CMD_INIT, sizeof(CMD_INIT), pdMS_TO_TICKS(50));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AHT20 niet gevonden op 0x%02X (err=0x%x)", AHT20_I2C_ADDR, ret);
        return ESP_ERR_NOT_FOUND;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "AHT20 geïnitialiseerd");
    return ESP_OK;
}

esp_err_t aht20_read(float *temp_c, float *hum_pct)
{
    if (!s_dev || !temp_c || !hum_pct) return ESP_ERR_INVALID_STATE;

    /* Trigger meting */
    esp_err_t ret = i2c_master_transmit(s_dev, CMD_TRIGGER, sizeof(CMD_TRIGGER), pdMS_TO_TICKS(50));
    if (ret != ESP_OK) return ret;

    /* Wacht op voltooiing (datasheet: 80 ms typisch) */
    vTaskDelay(pdMS_TO_TICKS(85));

    /* Lees status en data – poll busy-bit max 3×  */
    uint8_t buf[6] = {0};
    for (int attempt = 0; attempt < 3; attempt++) {
        ret = i2c_master_receive(s_dev, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (ret != ESP_OK) return ret;
        if (!(buf[0] & 0x80)) break;          /* bit 7 = 0 → klaar */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (buf[0] & 0x80) {
        ESP_LOGW(TAG, "AHT20 timeout – sensor nog bezig");
        return ESP_ERR_TIMEOUT;
    }

    /* Decodeer 20-bits waarden */
    uint32_t raw_hum  = ((uint32_t)buf[1] << 12) |
                        ((uint32_t)buf[2] <<  4) |
                        ((uint32_t)buf[3] >>  4);

    uint32_t raw_temp = ((uint32_t)(buf[3] & 0x0F) << 16) |
                        ((uint32_t)buf[4] <<  8) |
                        ((uint32_t)buf[5]);

    *hum_pct = (float)raw_hum  / 1048576.0f * 100.0f;
    *temp_c  = (float)raw_temp / 1048576.0f * 200.0f - 50.0f;

    ESP_LOGD(TAG, "temp=%.1f°C  hum=%.1f%%", *temp_c, *hum_pct);
    return ESP_OK;
}
