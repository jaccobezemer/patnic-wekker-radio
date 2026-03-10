#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <time.h>

#include "nvs_settings.h"
#include "wifi_manager.h"
#include "captive_portal.h"
#include "uart_ctrl.h"
#include "radio_player.h"
#include "weather.h"

static const char *TAG = "main";

/* ------------------------------------------------------------------ */
/* UART callbacks — aangeroepen vanuit uart_rx_task                      */
/* ------------------------------------------------------------------ */

static void on_play(void)
{
    if (radio_player_get_status() == RADIO_STATUS_PAUSED) {
        radio_player_resume();
    }
    uart_ctrl_send_status((uint8_t)radio_player_get_status(),
                          (uint8_t)radio_player_get_volume());
}

static void on_stop(void)
{
    radio_player_stop();
    uart_ctrl_send_status(WEKKER_STATUS_STOPPED, (uint8_t)radio_player_get_volume());
}

static void on_pause(void)
{
    radio_player_pause();
    uart_ctrl_send_status(WEKKER_STATUS_PAUSED, (uint8_t)radio_player_get_volume());
}

static void on_volume(uint8_t vol)
{
    radio_player_set_volume(vol);
    uart_ctrl_send_status((uint8_t)radio_player_get_status(),
                          (uint8_t)radio_player_get_volume());
}

static void on_set_url(const char *url)
{
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi niet verbonden — stream kan niet starten");
        uart_ctrl_send_status(WEKKER_STATUS_ERROR, (uint8_t)radio_player_get_volume());
        return;
    }
    radio_player_play_url(url);
    uart_ctrl_send_status(WEKKER_STATUS_PLAYING, (uint8_t)radio_player_get_volume());
}

static void on_status_request(void)
{
    uart_ctrl_send_status((uint8_t)radio_player_get_status(),
                          (uint8_t)radio_player_get_volume());
}

static void on_eq(uint8_t band, int8_t gain)
{
    /* ES8388 heeft geen software EQ API — verzoek wordt gelogd */
    ESP_LOGI(TAG, "EQ: band=%d gain=%d dB (niet ondersteund door ES8388 driver)",
             band, gain);
}

static void on_time_request(void)
{
    time_t now = 0;
    time(&now);
    uart_ctrl_send_time((uint32_t)now);
    ESP_LOGI(TAG, "Tijdverzoek beantwoord: epoch=%lld", (long long)now);
}

static void on_weather_request(void)
{
    weather_send_cached();
}

/* ------------------------------------------------------------------ */
/* WiFi events                                                            */
/* ------------------------------------------------------------------ */

static void wifi_callback(wifi_mgr_event_t event, void *arg)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WiFi verbonden — IP: %s", wifi_manager_get_ip_str());
            break;

        case WIFI_MGR_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi verbroken");
            break;

        case WIFI_MGR_EVENT_CONNECT_FAILED:
        case WIFI_MGR_EVENT_NO_CREDENTIALS:
            ESP_LOGW(TAG, "Geen WiFi verbinding — SoftAP + captive portal starten");
            wifi_manager_start_ap();
            captive_portal_start();
            break;
    }
}

/* ------------------------------------------------------------------ */
/* app_main                                                               */
/* ------------------------------------------------------------------ */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Patnic Wekker-Radio ===");

    /* 1. NVS initialiseren */
    ESP_ERROR_CHECK(nvs_settings_init());

    /* 2. Radio player (ES8388 + I2S via esp_codec_dev) */
    ESP_ERROR_CHECK(radio_player_init());

    /* 3. UART besturing */
    static const uart_ctrl_callbacks_t uart_cb = {
        .on_play           = on_play,
        .on_stop           = on_stop,
        .on_pause          = on_pause,
        .on_volume         = on_volume,
        .on_set_url        = on_set_url,
        .on_status_request = on_status_request,
        .on_eq             = on_eq,
        .on_time_request    = on_time_request,
        .on_weather_request = on_weather_request,
    };
    ESP_ERROR_CHECK(uart_ctrl_init(&uart_cb));

    /* 4. Weer-fetch taak */
    ESP_ERROR_CHECK(weather_init());

    /* 5. WiFi (async — resultaat via wifi_callback) */
    ESP_ERROR_CHECK(wifi_manager_init(wifi_callback));
    ESP_ERROR_CHECK(wifi_manager_start());

    ESP_LOGI(TAG, "=== Initialisatie klaar — wacht op WiFi ===");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
