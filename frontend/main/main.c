/*****************************************************************************
 * | File       :   main.c
 * | Author     :   Waveshare team
 * | Function   :   Main function
 * | Info       :   
 * |                Demonstrates an LVGL slider to control LED brightness.
 *----------------
 * | Version    :   V1.0
 * | Date       :   2024-12-07
 * | Info       :   Basic version
 *
 ******************************************************************************/

#include "rgb_lcd_port.h"    // Header for Waveshare RGB LCD driver
#include "gt911.h"           // Header for touch screen operations (GT911)
#include "lvgl_port.h"       // Header for LVGL port initialization and locking
#include "wekker_ui.h"       // Wekker hoofdscherm (klok, sensoren, sliders)
#include "lyrat_ctrl.h"      // LyraT 4.3 UART-communicatieprotocol
#include "tsl2591.h"         // TSL2591 lichtsensor (I2C 0x29)
#include "aht20.h"           // AHT20 temperatuur/vochtigheid (I2C 0x38)
#include "i2c.h"             // Gedeelde I2C bus (DEV_I2C_Get_Bus)
#include <sys/time.h>        // gettimeofday / settimeofday
#include <time.h>            // time()
#include <string.h>          // strstr

static const char *TAG = "main";  // Tag used for ESP log output

/* ── Sensor-gegevens (gedeeld met UI-taak) ───────────────────────────────── */

volatile float g_lux      = 0.0f;
volatile float g_temp_c   = 0.0f;
volatile float g_hum_pct  = 0.0f;

/* ── Buiten weerdata (ontvangen via LyraT UART) ──────────────────────────── */

volatile float g_out_temp_c  = -999.0f;   /* -999 = nog geen data */
volatile int   g_out_hum_pct = -1;
volatile int   g_out_wcode   = -1;        /* WMO weather code     */
volatile float g_out_wind_ms = -999.0f;

/* ── LyraT callback-functies ─────────────────────────────────────────────── */

static void on_lyrat_status(lyrat_status_t status, uint8_t volume)
{
    const char *status_str[] = { "GESTOPT", "SPEELT", "GEPAUZEERD" };
    const char *s = (status <= LYRAT_PAUSED) ? status_str[status] : "FOUT";
    ESP_LOGI(TAG, "LyraT status: %s  volume: %d", s, volume);
}

static void on_lyrat_audio_level(uint8_t peak_l, uint8_t peak_r)
{
    /* Hier kunnen VU-meter widgets worden bijgewerkt */
    ESP_LOGD(TAG, "VU: L=%d R=%d", peak_l, peak_r);
}

static volatile bool g_time_synced = false;

static void on_lyrat_weather(const char *json)
{
    const char *p;
    float temp = -999.0f;
    int   hum  = -1, wcode = -1;
    float wind = -999.0f;

    if ((p = strstr(json, "\"temp\":")))  sscanf(p + 7, "%f", &temp);
    if ((p = strstr(json, "\"hum\":")))   sscanf(p + 6, "%d", &hum);
    if ((p = strstr(json, "\"wcode\":"))) sscanf(p + 8, "%d", &wcode);
    if ((p = strstr(json, "\"wind\":")))  sscanf(p + 7, "%f", &wind);

    g_out_temp_c  = temp;
    g_out_hum_pct = hum;
    g_out_wcode   = wcode;
    g_out_wind_ms = wind;
    ESP_LOGI(TAG, "Buiten: %.1f°C  %d%%  wcode=%d  wind=%.1f m/s",
             (double)temp, hum, wcode, (double)wind);
}

static void on_lyrat_time_sync(uint32_t utc_epoch)
{
    struct timeval tv = { .tv_sec = utc_epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    g_time_synced = true;
    ESP_LOGI(TAG, "Systeemklok gesynchroniseerd via LyraT: epoch=%lu",
             (unsigned long)utc_epoch);
}

/* ── Sensor-taak: leest TSL2591 en AHT20 elke 10 seconden ───────────────── */

static void sensor_task(void *arg)
{
    while (1) {
        float lux = 0.0f;
        if (tsl2591_read_lux(&lux) == ESP_OK) {
            g_lux = lux;
            ESP_LOGI(TAG, "Lux: %.1f", lux);
        }

        float temp = 0.0f, hum = 0.0f;
        if (aht20_read(&temp, &hum) == ESP_OK) {
            g_temp_c  = temp;
            g_hum_pct = hum;
            ESP_LOGI(TAG, "Temp: %.1f°C  Vochtigheid: %.1f%%", temp, hum);
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// Main application function
void app_main()
{
    static esp_lcd_panel_handle_t panel_handle = NULL; // LCD panel handle
    static esp_lcd_touch_handle_t tp_handle = NULL;    // Touch panel handle

    // Tijdzone instellen (Nederland: CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    // Initialize the GT911 touch screen controller
    tp_handle = touch_gt911_init();  
    
    // Initialiseer I2C-sensoren (bus is aangemaakt door touch_gt911_init())
    i2c_master_bus_handle_t i2c_bus = DEV_I2C_Get_Bus();
    tsl2591_init(i2c_bus, TSL2591_GAIN_MED, TSL2591_ITIME_100MS);
    // aht20_init(i2c_bus);
    xTaskCreate(sensor_task, "sensors", 3072, NULL, 4, NULL);

    // Initialize the Waveshare ESP32-S3 RGB LCD hardware
    panel_handle = waveshare_esp32_s3_rgb_lcd_init();

    // Turn on the LCD backlight
    wavesahre_rgb_lcd_bl_on();   

    // Initialize the LVGL library with the panel and touch handles
    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));

    // Initialize LyraT 4.3 audio board communication (GPIO15=TX, GPIO16=RX)
    const lyrat_callbacks_t lyrat_cbs = {
        .on_status      = on_lyrat_status,
        .on_audio_level = on_lyrat_audio_level,
        .on_time_sync   = on_lyrat_time_sync,
        .on_weather     = on_lyrat_weather,
    };
    ESP_ERROR_CHECK(lyrat_ctrl_init(&lyrat_cbs));

    // Vraag status en gecachede weerdata op bij opstart
    lyrat_request_status();
    lyrat_request_weather();

    // Bouw het wekker-scherm op
    if (lvgl_port_lock(-1)) {
        wekker_ui_init();
        lvgl_port_unlock();
    }
    uint32_t tick = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        tick++;

        if (!g_time_synced) {
            // Nog niet gesynchroniseerd: probeer elke 10 seconden
            if (tick % 10 == 0) {
                ESP_LOGI(TAG, "Tijd aanvragen bij LyraT (poging %lu)...", (unsigned long)(tick / 10));
                lyrat_request_time();
            }
        } else {
            // Gesynchroniseerd: hersynchen elke 5 minuten
            if (tick % 300 == 0) {
                lyrat_request_time();
            }
        }
    }
    
}
