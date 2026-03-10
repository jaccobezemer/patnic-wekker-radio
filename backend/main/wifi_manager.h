#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    WIFI_MGR_EVENT_CONNECTED,       /* STA verbonden, IP ontvangen, SNTP gestart */
    WIFI_MGR_EVENT_DISCONNECTED,    /* STA verbinding verbroken                  */
    WIFI_MGR_EVENT_CONNECT_FAILED,  /* Max retries bereikt                       */
    WIFI_MGR_EVENT_NO_CREDENTIALS,  /* Geen SSID opgeslagen in NVS               */
} wifi_mgr_event_t;

typedef void (*wifi_mgr_callback_t)(wifi_mgr_event_t event, void *arg);

/**
 * Initialiseer WiFi hardware en event loop.
 * Moet aangeroepen worden na nvs_settings_init().
 */
esp_err_t wifi_manager_init(wifi_mgr_callback_t callback);

/**
 * Laad credentials uit NVS en probeer als STA te verbinden.
 * Events worden asynchroon gemeld via de callback.
 */
esp_err_t wifi_manager_start(void);

/**
 * Stop STA, start SoftAP ("wekker-XXXXXX", open netwerk).
 * Roep daarna captive_portal_start() aan.
 */
esp_err_t wifi_manager_start_ap(void);

/**
 * Sla nieuwe WiFi credentials op in NVS.
 * (Wordt aangeroepen vanuit de captive portal POST handler.)
 */
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

/** True als STA momenteel verbonden is. */
bool wifi_manager_is_connected(void);

/** Geeft het huidige IP-adres als string, of NULL als niet verbonden. */
const char *wifi_manager_get_ip_str(void);
