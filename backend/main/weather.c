#include "weather.h"
#include "uart_ctrl.h"
#include "wifi_manager.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "weather";

#define FETCH_INTERVAL_MS   (10 * 60 * 1000)   /* elke 10 minuten */
#define HTTP_TIMEOUT_MS     10000
#define HTTP_BUF_SIZE       2048

/* Gecachede JSON string voor CMD_REQUEST_WEATHER */
static char s_cached_json[256] = {0};

/* ── Open-Meteo URL ─────────────────────────────────────────────── */
static const char *WEATHER_URL =
    "http://api.open-meteo.com/v1/forecast"
    "?latitude="  CONFIG_WEATHER_LATITUDE
    "&longitude=" CONFIG_WEATHER_LONGITUDE
    "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
    "&forecast_days=1";

/* ── HTTP response buffer ───────────────────────────────────────── */
static char s_http_buf[HTTP_BUF_SIZE];
static int  s_http_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (s_http_len + evt->data_len < (int)sizeof(s_http_buf) - 1) {
                memcpy(s_http_buf + s_http_len, evt->data, evt->data_len);
                s_http_len += evt->data_len;
            }
            break;
        case HTTP_EVENT_ON_CONNECTED:
            s_http_len = 0;
            memset(s_http_buf, 0, sizeof(s_http_buf));
            break;
        default:
            break;
    }
    return ESP_OK;
}

/* ── Fetch + parse + verstuur ───────────────────────────────────── */
static void fetch_and_send(void)
{
    if (!wifi_manager_is_connected()) return;

    esp_http_client_config_t cfg = {
        .url         = WEATHER_URL,
        .timeout_ms  = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return;

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "HTTP fout: err=%s status=%d", esp_err_to_name(err), status);
        return;
    }

    s_http_buf[s_http_len] = '\0';

    /* Parse JSON met cJSON */
    cJSON *root = cJSON_Parse(s_http_buf);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse mislukt");
        return;
    }

    float temp     = NAN;
    int   humidity = -1;
    int   wcode    = -1;
    float wind     = NAN;

    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (current) {
        cJSON *t  = cJSON_GetObjectItem(current, "temperature_2m");
        cJSON *h  = cJSON_GetObjectItem(current, "relative_humidity_2m");
        cJSON *wc = cJSON_GetObjectItem(current, "weather_code");
        cJSON *ws = cJSON_GetObjectItem(current, "wind_speed_10m");
        if (cJSON_IsNumber(t))  temp     = (float)t->valuedouble;
        if (cJSON_IsNumber(h))  humidity = h->valueint;
        if (cJSON_IsNumber(wc)) wcode    = wc->valueint;
        if (cJSON_IsNumber(ws)) wind     = (float)ws->valuedouble;
    }
    cJSON_Delete(root);

    if (isnan(temp)) {
        ESP_LOGW(TAG, "Temperatuur ontbreekt in response");
        return;
    }

    /* Bouw compacte JSON voor UART */
    snprintf(s_cached_json, sizeof(s_cached_json),
             "{\"temp\":%.1f,\"hum\":%d,\"wcode\":%d,\"wind\":%.1f}",
             temp, humidity, wcode, isnan(wind) ? 0.0f : wind);

    ESP_LOGI(TAG, "Weerdata: %s", s_cached_json);
    uart_ctrl_send_weather(s_cached_json);
}

/* ── Periodieke taak ────────────────────────────────────────────── */
static void weather_task(void *pvParam)
{
    /* Eerste fetch direct na WiFi-verbinding (korte delay voor stabiliteit) */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        fetch_and_send();
        vTaskDelay(pdMS_TO_TICKS(FETCH_INTERVAL_MS));
    }
}

/* ── Publieke API ───────────────────────────────────────────────── */

esp_err_t weather_init(void)
{
    BaseType_t r = xTaskCreatePinnedToCore(
        weather_task, "weather", 6144, NULL, 3, NULL, 0);
    return (r == pdPASS) ? ESP_OK : ESP_FAIL;
}

void weather_send_cached(void)
{
    if (s_cached_json[0] != '\0') {
        uart_ctrl_send_weather(s_cached_json);
    }
}
