/*****************************************************************************
 * | File         :   tsl2591.c
 * | Function     :   TSL2591 digitale lichtsensor driver (I2C 0x29)
 * ----------------
 * | Version      :   V1.0
 * | Date         :   2026-03-08
 *
 ******************************************************************************/

#include "tsl2591.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "tsl2591";

/* ── Registeradressen ───────────────────────────────────────────────────── */

/* Elk register-toegang vereist een commandobyte: 0x80 | reg_addr.
 * Voor blok-lezen (auto-increment): 0xA0 | reg_addr.               */
#define CMD_NORMAL(reg)     (0x80 | (reg))
#define CMD_BLOCK(reg)      (0xA0 | (reg))

#define REG_ENABLE          0x00
#define REG_CONTROL         0x01
#define REG_ID              0x12  /* Device ID – moet 0x50 zijn */
#define REG_C0DATAL         0x14  /* Channel 0 (full spectrum) low byte  */

/* ENABLE register bits */
#define ENABLE_PON          0x01  /* Power ON                */
#define ENABLE_AEN          0x02  /* ALS Enable              */

/* Lux-berekening constante (uit Adafruit TSL2591 bibliotheek) */
#define LUX_DF              408.0f

/* Gain-factor lookup (correspondeert met tsl2591_gain_t) */
static const float GAIN_FACTOR[] = { 1.0f, 25.0f, 428.0f, 9876.0f };

/* Integratietime in ms lookup (correspondeert met tsl2591_itime_t) */
static const float ITIME_MS[] = { 100.0f, 200.0f, 300.0f, 400.0f, 500.0f, 600.0f };

/* ── Module-state ───────────────────────────────────────────────────────── */

static i2c_master_dev_handle_t s_dev = NULL;
static tsl2591_gain_t          s_gain  = TSL2591_GAIN_MED;
static tsl2591_itime_t         s_itime = TSL2591_ITIME_100MS;

/* ── Interne helpers ────────────────────────────────────────────────────── */

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { CMD_NORMAL(reg), value };
    return i2c_master_transmit(s_dev, buf, 2, pdMS_TO_TICKS(50));
}

static esp_err_t read_reg(uint8_t reg, uint8_t *out)
{
    uint8_t cmd = CMD_NORMAL(reg);
    return i2c_master_transmit_receive(s_dev, &cmd, 1, out, 1, pdMS_TO_TICKS(50));
}

/* Lees 4 bytes channel-data via auto-increment starting at CH0DATAL */
static esp_err_t read_channels(uint16_t *ch0, uint16_t *ch1)
{
    uint8_t cmd = CMD_BLOCK(REG_C0DATAL);
    uint8_t buf[4] = {0};
    esp_err_t ret = i2c_master_transmit_receive(s_dev, &cmd, 1, buf, 4, pdMS_TO_TICKS(50));
    if (ret == ESP_OK) {
        *ch0 = (uint16_t)(buf[1] << 8 | buf[0]);
        *ch1 = (uint16_t)(buf[3] << 8 | buf[2]);
    }
    return ret;
}

/* ── Publieke functies ──────────────────────────────────────────────────── */

bool tsl2591_probe(void)
{
    if (!s_dev) return false;
    uint8_t id = 0;
    esp_err_t ret = read_reg(REG_ID, &id);
    return (ret == ESP_OK && id == 0x50);
}

esp_err_t tsl2591_init(i2c_master_bus_handle_t bus,
                        tsl2591_gain_t gain,
                        tsl2591_itime_t itime)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    s_gain  = gain;
    s_itime = itime;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = TSL2591_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev));

    if (!tsl2591_probe()) {
        ESP_LOGE(TAG, "TSL2591 niet gevonden op 0x%02X", TSL2591_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    /* ALS power on + enable */
    ESP_ERROR_CHECK(write_reg(REG_ENABLE, ENABLE_PON | ENABLE_AEN));
    /* Gain + integratietime instellen */
    ESP_ERROR_CHECK(write_reg(REG_CONTROL, (uint8_t)(gain | itime)));

    /* Wacht integratietime + marge voor eerste meting */
    vTaskDelay(pdMS_TO_TICKS((uint32_t)ITIME_MS[itime] + 20));

    ESP_LOGI(TAG, "TSL2591 geïnitialiseerd – gain=%d itime=%.0f ms",
             (int)GAIN_FACTOR[gain >> 4], ITIME_MS[itime]);
    return ESP_OK;
}

esp_err_t tsl2591_read_lux(float *lux)
{
    if (!s_dev || !lux) return ESP_ERR_INVALID_STATE;

    uint16_t ch0 = 0, ch1 = 0;
    esp_err_t ret = read_channels(&ch0, &ch1);
    if (ret != ESP_OK) return ret;

    /* Overbelichting (saturatie) */
    if (ch0 == 0xFFFF || ch1 == 0xFFFF) {
        *lux = 0.0f;
        ESP_LOGW(TAG, "Sensor verzadigd");
        return ESP_OK;
    }

    float gain_f  = GAIN_FACTOR[s_gain >> 4];
    float itime_f = ITIME_MS[s_itime];
    float cpl     = (itime_f * gain_f) / LUX_DF;

    float lux1 = ((float)ch0 - 1.64f * (float)ch1) / cpl;
    float lux2 = (0.59f * (float)ch0 - 0.86f * (float)ch1) / cpl;

    *lux = (lux1 > lux2) ? lux1 : lux2;
    if (*lux < 0.0f) *lux = 0.0f;

    ESP_LOGD(TAG, "CH0=%u CH1=%u lux=%.1f", ch0, ch1, *lux);
    return ESP_OK;
}
