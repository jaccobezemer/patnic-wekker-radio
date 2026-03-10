#pragma once

#include "esp_err.h"

/** Start de captive portal HTTP server (port 80). */
esp_err_t captive_portal_start(void);

/** Stop de captive portal HTTP server. */
esp_err_t captive_portal_stop(void);
