#include "wifi_manager.h"
#include "nvs_settings.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RETRY           5

static esp_netif_t         *s_sta_netif        = NULL;
static wifi_mgr_callback_t  s_callback        = NULL;
static bool                 s_connected       = false;
static char                 s_ip_str[16]      = {0};
static EventGroupHandle_t   s_wifi_event_group;
static int                  s_retry_count     = 0;

/* ── SNTP ───────────────────────────────────────────────────────── */
static void sntp_start(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP gestart (pool.ntp.org / time.google.com)");
}

/* ── WiFi event handler ─────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_retry_count++;

        if (s_callback) s_callback(WIFI_MGR_EVENT_DISCONNECTED, NULL);

        if (s_retry_count >= MAX_RETRY) {
            ESP_LOGW(TAG, "Max retries (%d) bereikt, stoppen met verbinden", MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            if (s_callback) s_callback(WIFI_MGR_EVENT_CONNECT_FAILED, NULL);
            return;
        }

        int delay_s = s_retry_count < 5 ? s_retry_count : 5;
        ESP_LOGW(TAG, "Verbroken, herverbinding %d/%d in %ds...",
                 s_retry_count, MAX_RETRY, delay_s);
        vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "WiFi verbonden — IP: %s", s_ip_str);
        s_connected   = true;
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        sntp_start();
        if (s_callback) s_callback(WIFI_MGR_EVENT_CONNECTED, NULL);
    }
}

/* ── Publieke API ───────────────────────────────────────────────── */

esp_err_t wifi_manager_init(wifi_mgr_callback_t callback)
{
    s_callback        = callback;
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_sta_netif, CONFIG_DEVICE_HOSTNAME);
    ESP_LOGI(TAG, "Hostname: %s", CONFIG_DEVICE_HOSTNAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    char ssid[33]     = {0};
    char password[65] = {0};

    esp_err_t ret = nvs_settings_get_wifi(ssid, sizeof(ssid),
                                          password, sizeof(password));
    if (ret != ESP_OK || ssid[0] == '\0') {
        ESP_LOGW(TAG, "Geen WiFi credentials in NVS — AP modus vereist");
        if (s_callback) s_callback(WIFI_MGR_EVENT_NO_CREDENTIALS, NULL);
        return ESP_OK;
    }

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid,     ssid,     sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode =
        (strlen(password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Verbinden met '%s'...", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(void)
{
    /* Bouw SSID op met laatste 3 bytes van MAC */
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s-%02X%02X%02X",
             CONFIG_SOFTAP_SSID, mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Start SoftAP '%s'...", ap_ssid);

    esp_wifi_stop();
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid_len       = 0,      /* auto */
            .max_connection = 4,
            .authmode       = WIFI_AUTH_OPEN,
        },
    };
    strlcpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP actief — verbind met '%s' en open http://192.168.4.1", ap_ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password)
{
    return nvs_settings_set_wifi(ssid, password);
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

const char *wifi_manager_get_ip_str(void)
{
    return (s_ip_str[0] != '\0') ? s_ip_str : NULL;
}
