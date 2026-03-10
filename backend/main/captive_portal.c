#include "captive_portal.h"
#include "wifi_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "captive_portal";
static httpd_handle_t s_portal_server = NULL;

/* ── URL decoder (handles + en %XX) ────────────────────────────── */
static void url_decode(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    while (*src && i < max - 1) {
        if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

/* ── Setup pagina ───────────────────────────────────────────────── */
static const char PORTAL_PAGE[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>patnic-wekker-wadio WiFi Setup</title>"
    "<style>"
    "body{font-family:sans-serif;background:#1e1e1e;color:#ccc;"
    "display:flex;justify-content:center;align-items:center;"
    "min-height:100vh;margin:0}"
    ".box{background:#2a2a2a;padding:2em;border-radius:12px;"
    "text-align:center;max-width:380px;width:90%}"
    "h2{color:#FF9800;margin-top:0}"
    ".sub{color:#888;font-size:13px;margin-bottom:1.5em}"
    "label{display:block;text-align:left;margin-bottom:4px;"
    "font-size:14px;color:#aaa}"
    "input{width:100%;box-sizing:border-box;background:#333;color:#fff;"
    "border:1px solid #555;border-radius:6px;padding:10px;"
    "font-size:14px;margin-bottom:1em}"
    "button{background:#4CAF50;color:#fff;border:none;"
    "padding:12px 32px;border-radius:6px;font-size:16px;"
    "cursor:pointer;width:100%}"
    "</style></head><body><div class='box'>"
    "<h2>WiFi Instellen</h2>"
    "<p class='sub'>Verbind Wekker-Radio met je netwerk</p>"
    "<form method='post' action='/'>"
    "<label>WiFi naam (SSID)</label>"
    "<input type='text' name='ssid' placeholder='MijnNetwerk' required>"
    "<label>Wachtwoord</label>"
    "<input type='password' name='password' "
    "placeholder='Wachtwoord (leeg = open netwerk)'>"
    "<button type='submit'>Opslaan &amp; Verbinden</button>"
    "</form></div></body></html>";

static esp_err_t portal_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, PORTAL_PAGE);
    return ESP_OK;
}

/* ── Herstart na delay ──────────────────────────────────────────── */
static void portal_restart_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

/* ── POST: credentials opslaan en herstart ──────────────────────── */
static esp_err_t portal_post_handler(httpd_req_t *req)
{
    char body[256] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[len] = '\0';

    char ssid_enc[64]  = {0};
    char pass_enc[128] = {0};
    httpd_query_key_value(body, "ssid",     ssid_enc, sizeof(ssid_enc));
    httpd_query_key_value(body, "password", pass_enc, sizeof(pass_enc));

    char ssid[33]     = {0};
    char password[65] = {0};
    url_decode(ssid,     ssid_enc, sizeof(ssid));
    url_decode(password, pass_enc, sizeof(password));

    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID mag niet leeg zijn");
        return ESP_FAIL;
    }

    wifi_manager_set_credentials(ssid, password);
    ESP_LOGI(TAG, "Credentials opgeslagen (SSID='%s'), herstart...", ssid);

    static const char SUCCESS_PRE[] =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Opgeslagen</title>"
        "<style>body{font-family:sans-serif;background:#1e1e1e;color:#ccc;"
        "display:flex;justify-content:center;align-items:center;"
        "min-height:100vh;margin:0}"
        ".box{background:#2a2a2a;padding:2em;border-radius:12px;"
        "text-align:center;max-width:380px;width:90%%}"
        "h2{color:#4CAF50;margin-top:0}</style></head><body>"
        "<div class='box'><h2>Opgeslagen!</h2>"
        "<p>Verbinding wordt gemaakt met <strong>";
    static const char SUCCESS_POST[] =
        "</strong>.<br>Wekker-Radio herstart nu...</p>"
        "</div></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, SUCCESS_PRE, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, ssid, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, SUCCESS_POST, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);

    /* Herstart na 2 seconden zodat antwoord eerst verstuurd wordt */
    const esp_timer_create_args_t restart_args = {
        .callback = portal_restart_cb,
        .name     = "portal_rst",
    };
    esp_timer_handle_t restart_timer;
    if (esp_timer_create(&restart_args, &restart_timer) == ESP_OK) {
        esp_timer_start_once(restart_timer, 2000 * 1000);
    } else {
        esp_restart();
    }

    return ESP_OK;
}

/* ── Publieke API ───────────────────────────────────────────────── */
esp_err_t captive_portal_start(void)
{
    if (s_portal_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size  = 4096;

    esp_err_t err = httpd_start(&s_portal_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Portal server starten mislukt: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t get_uri  = {
        .uri = "/", .method = HTTP_GET,  .handler = portal_get_handler
    };
    httpd_uri_t post_uri = {
        .uri = "/", .method = HTTP_POST, .handler = portal_post_handler
    };
    httpd_register_uri_handler(s_portal_server, &get_uri);
    httpd_register_uri_handler(s_portal_server, &post_uri);

    ESP_LOGI(TAG, "Captive portal actief — open http://192.168.4.1 in de browser");
    return ESP_OK;
}

esp_err_t captive_portal_stop(void)
{
    if (!s_portal_server) return ESP_OK;
    httpd_stop(s_portal_server);
    s_portal_server = NULL;
    return ESP_OK;
}
