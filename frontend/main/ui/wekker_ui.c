/*****************************************************************************
 * | File       :   wekker_ui.c
 * | Function   :   LVGL hoofdscherm – klok, temp/hum, volume- en helderheidsslider
 *----------------
 * | Version    :   V1.1
 * | Date       :   2026-03-08
 *
 ******************************************************************************/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wekker_ui.h"
#include "gpio.h"               // Header for custom GPIO management functions
#include "lvgl.h"               // Header for the LVGL graphics library
#include "io_extension.h"       // Include IO_EXTENSION driver header for GPIO functions
#include "lyrat_ctrl.h"         // LyraT UART volume control
#include <time.h>               // localtime, strftime
#include "esp_log.h"
#include "lv_font_montserrat_120_digits.h"
#include <stdio.h>

static const char *TAG = "wekker_ui";

extern volatile float g_temp_c;       // Temperatuur van AHT20 (°C)
extern volatile float g_hum_pct;      // Luchtvochtigheid van AHT20 (%)
extern volatile float g_out_temp_c;   // Buiten temperatuur (Open-Meteo via LyraT)
extern volatile int   g_out_hum_pct;  // Buiten luchtvochtigheid
extern volatile int   g_out_wcode;    // WMO weather code
extern volatile float g_out_wind_ms;  // Windsnelheid (m/s)

static lv_obj_t * label;              // Waarde-label helderheidsslider
static lv_obj_t * volume_label;       // Waarde-label volumeslider
static lv_obj_t * temp_label;         // Indoor temperatuurwaarde
static lv_obj_t * hum_label;          // Indoor vochtigheidwaarde
static lv_obj_t * out_desc_label;     // Buiten: weerbeschrijving
static lv_obj_t * out_env_label;      // Buiten: temp + vochtigheid
static lv_obj_t * out_wind_label;     // Buiten: windsnelheid
static lv_obj_t * clk_roller[6];      // Flip-klok cijfers: H1 H2 M1 M2 S1 S2

/* WMO weather code → korte Nederlandse omschrijving */
static const char *wmo_desc(int code)
{
    if (code < 0)                        return "onbekend";
    if (code == 0)                       return "helder";
    if (code <= 3)                       return "bewolkt";
    if (code == 45 || code == 48)        return "mist";
    if (code >= 51 && code <= 55)        return "motregen";
    if (code >= 61 && code <= 65)        return "regen";
    if (code >= 66 && code <= 67)        return "ijzel";
    if (code >= 71 && code <= 77)        return "sneeuw";
    if (code >= 80 && code <= 82)        return "buien";
    if (code >= 85 && code <= 86)        return "sneeuwbuien";
    if (code >= 95)                      return "onweer";
    return "onbekend";
}

/**
 * @brief  Event callback function for slider interaction
 * @param  e: Pointer to the event descriptor
 */
static void slider_event_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e); // Get the slider object that triggered the event

    lv_label_set_text_fmt(label, LV_SYMBOL_IMAGE " %" LV_PRId32 "%%", lv_slider_get_value(slider));

    // Align the label above the slider
    lv_obj_align_to(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);

    // Set the PWM duty cycle to control the LED brightness (inverted duty cycle)
    DEV_SET_PWM(100 - lv_slider_get_value(slider));
    IO_EXTENSION_Pwm_Output(100 - lv_slider_get_value(slider));
}

/**
 * @brief Maak één cijfer-roller aan met flip-klok stijl.
 */
static lv_obj_t * make_digit_roller(lv_obj_t *parent)
{
    lv_obj_t *roller = lv_roller_create(parent);
    lv_roller_set_options(roller, "0\n1\n2\n3\n4\n5\n6\n7\n8\n9", LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller, 1);
    lv_obj_set_size(roller, 140, 160);
    // Niet-geselecteerde cijfers: zwart op zwart = onzichtbaar
    lv_obj_set_style_text_color(roller, lv_color_hex(0x000000), LV_PART_MAIN     | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(roller,  &lv_font_montserrat_120_digits, LV_PART_MAIN     | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(roller,   lv_color_hex(0x000000), LV_PART_MAIN     | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(roller,     LV_OPA_COVER,           LV_PART_MAIN     | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(roller, 0,                    LV_PART_MAIN     | LV_STATE_DEFAULT);
    // Geselecteerd cijfer: wit op donkergrijs kaartje
    lv_obj_set_style_text_color(roller, lv_color_hex(0xFFFFFF), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(roller,  &lv_font_montserrat_120_digits, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(roller,   lv_color_hex(0x333333), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(roller,     LV_OPA_COVER,           LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(roller,     8,                      LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(roller,    12,                     LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_anim_time(roller, 300, LV_PART_MAIN);
    return roller;
}

/**
 * @brief Timer-callback: ververs de flip-klok elke seconde.
 */
static void clock_cb(lv_timer_t * timer)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int digits[6] = {
        t->tm_hour / 10, t->tm_hour % 10,
        t->tm_min  / 10, t->tm_min  % 10,
        t->tm_sec  / 10, t->tm_sec  % 10,
    };
    for (int i = 0; i < 6; i++) {
        lv_roller_set_selected(clk_roller[i], (uint16_t)digits[i], LV_ANIM_ON);
    }
}

/**
 * @brief Timer-callback: ververs temperatuur- en vochtigheidslabels elke seconde.
 */
static void env_cb(lv_timer_t * timer)
{
    char buf[64];

    snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %.1f \xc2\xb0""C", (double)g_temp_c);
    lv_label_set_text(temp_label, buf);
    snprintf(buf, sizeof(buf), LV_SYMBOL_TINT " %.1f %%", (double)g_hum_pct);
    lv_label_set_text(hum_label, buf);

    if (g_out_temp_c > -998.0f) {
        snprintf(buf, sizeof(buf), "Buiten: %s", wmo_desc(g_out_wcode));
        lv_label_set_text(out_desc_label, buf);

        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %.1f \xc2\xb0""C  " LV_SYMBOL_TINT " %d%%",
                 (double)g_out_temp_c, (int)g_out_hum_pct);
        lv_label_set_text(out_env_label, buf);

        float wind = g_out_wind_ms > -998.0f ? g_out_wind_ms : 0.0f;
        snprintf(buf, sizeof(buf), LV_SYMBOL_UP " %.1f m/s", (double)wind);
        lv_label_set_text(out_wind_label, buf);
    }
}

/**
 * @brief Callback voor de volumeslider – stuurt het volume via UART naar de LyraT.
 */
static void volume_slider_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    int32_t vol = lv_slider_get_value(slider);

    lv_label_set_text_fmt(volume_label, "Volume: %" LV_PRId32, vol);
    lv_obj_align_to(volume_label, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);

    lyrat_set_volume((uint8_t)vol);
}

static const struct { const char *name; const char *url; } radio_stations[] = {
    { "NPO Radio 2", "http://icecast.omroep.nl/radio2-bb-mp3"                                                       },
    { "NPO Radio 5", "http://icecast.omroep.nl/radio5-bb-aac"                                                       },
    { "Radio 538",   "http://playerservices.streamtheworld.com/api/livestream-redirect/RADIO538AAC.aac"              },
    { "Qmusic",      "http://stream.qmusic.nl/qmusic/mp3"                                                           },
};

/**
 * @brief Callback voor een radiozenderknop – stuurt de URL naar de LyraT.
 */
static void radio_btn_cb(lv_event_t * e)
{
    const char *url = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Radio: %s", url);
    lyrat_set_url(url, true);
}

/**
 * @brief Callback voor de play-knop – stuurt play-commando naar de LyraT.
 */
static void play_btn_cb(lv_event_t * e)
{
    ESP_LOGI(TAG, "Play");
    lyrat_play();
}

/**
 * @brief Callback voor de pause-knop – stuurt pause-commando naar de LyraT.
 */
static void pause_btn_cb(lv_event_t * e)
{
    ESP_LOGI(TAG, "Pause");
    lyrat_pause();
}

/**
 * @brief Bouw het wekker-hoofdscherm op (klok, sensoren, sliders).
 *        Aanroepen binnen lvgl_port_lock().
 */
void wekker_ui_init(void)
{
    // ── Helderheid PWM initialisatie ──────────────────────────────────────
    DEV_GPIO_PWM(LED_GPIO_PIN, 1000);
    DEV_SET_PWM(100 - CONFIG_DEFAULT_BRIGHTNESS);
    IO_EXTENSION_Pwm_Output(100 - CONFIG_DEFAULT_BRIGHTNESS);

    // ── Zwarte achtergrond ────────────────────────────────────────────────
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);

    // ── Flip-klok – prominent in het midden ──────────────────────────────
    lv_obj_t *clock_cont = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(clock_cont);
    lv_obj_set_size(clock_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(clock_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(clock_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clock_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(clock_cont, 4, 0);
    lv_obj_align(clock_cont, LV_ALIGN_CENTER, 0, -60);

    clk_roller[0] = make_digit_roller(clock_cont);
    clk_roller[1] = make_digit_roller(clock_cont);

    lv_obj_t *col1 = lv_label_create(clock_cont);
    lv_label_set_text(col1, ":");
    lv_obj_set_style_text_font(col1, &lv_font_montserrat_120_digits, 0);
    lv_obj_set_style_text_color(col1, lv_color_hex(0xFFFFFF), 0);

    clk_roller[2] = make_digit_roller(clock_cont);
    clk_roller[3] = make_digit_roller(clock_cont);

    lv_obj_t *col2 = lv_label_create(clock_cont);
    lv_label_set_text(col2, ":");
    lv_obj_set_style_text_font(col2, &lv_font_montserrat_120_digits, 0);
    lv_obj_set_style_text_color(col2, lv_color_hex(0xFFFFFF), 0);

    clk_roller[4] = make_digit_roller(clock_cont);
    clk_roller[5] = make_digit_roller(clock_cont);

    lv_timer_create(clock_cb, 1000, NULL);

    // ── Temperatuur en luchtvochtigheid – linksonder de klok ─────────────
    temp_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0x00BFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(temp_label, LV_SYMBOL_CHARGE " -- °C");
    lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 20, 20);

    hum_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(hum_label, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hum_label, lv_color_hex(0x00BFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(hum_label, LV_SYMBOL_TINT " -- %");
    lv_obj_align_to(hum_label, temp_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    // ── Buiten weerdata – rechts naast indoor sensoren ───────────────────
    out_desc_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(out_desc_label, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(out_desc_label, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(out_desc_label, "Buiten: --");
    lv_obj_align_to(out_desc_label, temp_label, LV_ALIGN_OUT_RIGHT_TOP, 40, 0);

    out_env_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(out_env_label, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(out_env_label, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(out_env_label, LV_SYMBOL_CHARGE " -- \xc2\xb0""C  " LV_SYMBOL_TINT " --%");
    lv_obj_align_to(out_env_label, out_desc_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    out_wind_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(out_wind_label, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(out_wind_label, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(out_wind_label, LV_SYMBOL_UP " -- m/s");
    lv_obj_align_to(out_wind_label, out_env_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    lv_timer_create(env_cb, 1000, NULL);

    // ── Volume-slider – onderin gecentreerd ───────────────────────────────
    lv_obj_t * vol_slider = lv_slider_create(lv_scr_act());
    lv_obj_set_width(vol_slider, 300);
    lv_obj_align(vol_slider, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_slider_set_range(vol_slider, 0, 100);
    lv_slider_set_value(vol_slider, CONFIG_DEFAULT_VOLUME, LV_ANIM_OFF);
    lv_obj_add_event_cb(vol_slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    volume_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(volume_label, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(volume_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        char vbuf[16];
        snprintf(vbuf, sizeof(vbuf), "Volume: %d", CONFIG_DEFAULT_VOLUME);
        lv_label_set_text(volume_label, vbuf);
    }
    lv_obj_align_to(volume_label, vol_slider, LV_ALIGN_OUT_TOP_MID, 0, -10);

    lyrat_set_volume(CONFIG_DEFAULT_VOLUME);

    // ── Helderheidsslider – rechtsonder ──────────────────────────────────
    lv_obj_t * slider = lv_slider_create(lv_scr_act());
    lv_obj_set_width(slider, 160);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_RIGHT, -20, -40);
    lv_slider_set_value(slider, CONFIG_DEFAULT_BRIGHTNESS, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        char bbuf[16];
        snprintf(bbuf, sizeof(bbuf), LV_SYMBOL_IMAGE " %d%%", CONFIG_DEFAULT_BRIGHTNESS);
        lv_label_set_text(label, bbuf);
    }
    lv_obj_align_to(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -8);

    // ── Play / Pause knoppen – rechtsbovenin ─────────────────────────────
    lv_obj_t * play_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(play_btn, 60, 40);
    lv_obj_align(play_btn, LV_ALIGN_TOP_RIGHT, -90, 10);
    lv_obj_add_event_cb(play_btn, play_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * play_lbl = lv_label_create(play_btn);
    lv_label_set_text(play_lbl, LV_SYMBOL_PLAY);
    lv_obj_center(play_lbl);

    lv_obj_t * pause_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(pause_btn, 60, 40);
    lv_obj_align(pause_btn, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_add_event_cb(pause_btn, pause_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * pause_lbl = lv_label_create(pause_btn);
    lv_label_set_text(pause_lbl, LV_SYMBOL_PAUSE);
    lv_obj_center(pause_lbl);

    // ── Radiozender knoppen – linksonderin ───────────────────────────────
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(lv_scr_act());
        lv_obj_set_size(btn, 180, 40);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 10, -10 - (3 - i) * 50);
        lv_obj_add_event_cb(btn, radio_btn_cb, LV_EVENT_CLICKED, (void *)radio_stations[i].url);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, radio_stations[i].name);
        lv_obj_center(lbl);
    }
}
