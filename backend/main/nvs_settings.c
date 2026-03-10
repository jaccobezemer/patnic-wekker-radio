#include "nvs_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nvs_settings";

#define NVS_NS "radio_cfg"

esp_err_t nvs_settings_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partitie wissen en opnieuw initialiseren...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t nvs_settings_get_wifi(char *ssid, size_t ssid_len,
                                char *password, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "NVS namespace '%s' nog niet aangemaakt", NVS_NS);
        return ret;
    }

    size_t sl = ssid_len;
    ret = nvs_get_str(h, "ssid", ssid, &sl);
    if (ret == ESP_OK) {
        size_t pl = pass_len;
        ret = nvs_get_str(h, "password", password, &pl);
    }

    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_set_wifi(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS openen mislukt: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_set_str(h, "ssid",     ssid);
    nvs_set_str(h, "password", password ? password : "");
    ret = nvs_commit(h);
    nvs_close(h);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials opgeslagen (SSID='%s')", ssid);
    }
    return ret;
}
