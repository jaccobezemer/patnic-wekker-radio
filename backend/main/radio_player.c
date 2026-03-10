#include "radio_player.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/semphr.h"
#include <string.h>

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8388_codec.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec_default.h"

static const char *TAG = "radio";

/* ------------------------------------------------------------------ */
/* LyraT v4.3 hardware pinnen                                          */
/* ------------------------------------------------------------------ */
#define ES8388_I2C_SDA_PIN      18
#define ES8388_I2C_SCL_PIN      23
#define ES8388_I2C_PORT         I2C_NUM_0
#define I2S_MCLK_PIN            0
#define I2S_BCLK_PIN            5
#define I2S_LRCLK_PIN           25
#define I2S_DOUT_PIN            26
#define I2S_DIN_PIN             35
#define PA_ENABLE_PIN           21
#define HEADPHONE_DETECT_PIN    19

/* ------------------------------------------------------------------ */
/* Configuratie                                                         */
/* ------------------------------------------------------------------ */
#define HTTP_TASK_PRIO          18
#define DEC_TASK_PRIO           5
#define STREAM_BUFFER_SIZE      (32 * 1024)
#define STREAM_TRIGGER_LEVEL    512
#define HTTP_READ_BUF_SIZE      (4 * 1024)
#define DEC_IN_BUF_SIZE         (8 * 1024)
#define DEC_OUT_BUF_SIZE        (16 * 1024)
#define MAX_URL_LEN             512
#define I2S_DMA_DESC_NUM        16
#define I2S_DMA_FRAME_NUM       512
#define HTTP_TIMEOUT_MS         10000
#define HTTP_RECONNECT_DELAY_MS 3000

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */
static int              s_volume       = CONFIG_AUDIO_VOLUME;
static radio_status_t   s_status       = RADIO_STATUS_STOPPED;
static volatile bool    s_stop_request = false;
static char             s_current_url[MAX_URL_LEN] = {0};

static StreamBufferHandle_t          s_stream_buf  = NULL;
static SemaphoreHandle_t             s_ctrl_sem    = NULL;
static TaskHandle_t                  s_http_task   = NULL;
static TaskHandle_t                  s_dec_task    = NULL;
static volatile esp_http_client_handle_t s_http_client = NULL;

/* ------------------------------------------------------------------ */
/* Codec handles                                                        */
/* ------------------------------------------------------------------ */
static i2c_master_bus_handle_t  s_i2c_bus     = NULL;
static i2s_chan_handle_t        s_i2s_tx_chan  = NULL;
static esp_codec_dev_handle_t   s_codec        = NULL;
static uint32_t                 s_codec_rate   = 0;

/* ------------------------------------------------------------------ */
/* Intern: detecteer AAC via URL                                        */
/* ------------------------------------------------------------------ */
static bool str_contains_icase(const char *hay, const char *needle)
{
    if (!hay || !needle) return false;
    size_t nlen = strlen(needle);
    for (; *hay; hay++) {
        bool match = true;
        for (size_t i = 0; i < nlen; i++) {
            if ((hay[i] | 0x20) != (needle[i] | 0x20)) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

static esp_audio_simple_dec_type_t detect_dec_type_from_url(const char *url)
{
    if (str_contains_icase(url, ".aac") || str_contains_icase(url, "aac")) {
        ESP_LOGI(TAG, "Decoder: AAC");
        return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    }
    ESP_LOGI(TAG, "Decoder: MP3");
    return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
}

/* ------------------------------------------------------------------ */
/* Intern: sample rate instellen                                        */
/* ------------------------------------------------------------------ */
static void codec_set_rate(uint32_t rate)
{
    if (rate == 0 || rate == s_codec_rate) return;

    ESP_LOGI(TAG, "Sample rate: %lu → %lu Hz", s_codec_rate, rate);

    if (s_codec_rate == 0) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel         = 2,
            .sample_rate     = rate,
        };
        int err = esp_codec_dev_open(s_codec, &fs);
        if (err != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Codec open mislukt: %d", err);
            return;
        }
        esp_codec_dev_set_out_vol(s_codec, s_volume);
    } else {
        i2s_channel_disable(s_i2s_tx_chan);
        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
        esp_err_t err = i2s_channel_reconfig_std_clock(s_i2s_tx_chan, &clk);
        i2s_channel_enable(s_i2s_tx_chan);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S reconfig mislukt: %s", esp_err_to_name(err));
            return;
        }
    }

    s_codec_rate = rate;
    ESP_LOGI(TAG, "Sample rate ingesteld: %lu Hz", rate);
}

/* ------------------------------------------------------------------ */
/* HTTP streaming taak                                                  */
/* ------------------------------------------------------------------ */
static void http_stream_task(void *pvParam)
{
    const char *url = (const char *)pvParam;

    uint8_t *buf = malloc(HTTP_READ_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "HTTP buf alloc mislukt");
        s_http_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t config = {
        .url                   = url,
        .timeout_ms            = HTTP_TIMEOUT_MS,
        .buffer_size           = HTTP_READ_BUF_SIZE,
        .buffer_size_tx        = 512,
        .user_agent            = "ESP32-Radio/1.0",
        .disable_auto_redirect = false,
        .keep_alive_enable     = true,
        .keep_alive_idle       = 5,
        .keep_alive_interval   = 5,
        .keep_alive_count      = 3,
    };

    while (!s_stop_request) {
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "HTTP init mislukt");
            vTaskDelay(pdMS_TO_TICKS(HTTP_RECONNECT_DELAY_MS));
            continue;
        }
        s_http_client = client;

        esp_http_client_set_header(client, "Icy-MetaData", "0");

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "HTTP open mislukt: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(HTTP_RECONNECT_DELAY_MS));
            continue;
        }

        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d", status);

        /* Handmatige redirect afhandeling */
        if (status == 301 || status == 302 || status == 303) {
            esp_http_client_set_redirection(client);
            esp_http_client_close(client);
            err = esp_http_client_open(client, 0);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Redirect open mislukt: %s", esp_err_to_name(err));
                esp_http_client_cleanup(client);
                vTaskDelay(pdMS_TO_TICKS(HTTP_RECONNECT_DELAY_MS));
                continue;
            }
            esp_http_client_fetch_headers(client);
            status = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "Na redirect HTTP status: %d", status);
        }

        if (status != 200) {
            ESP_LOGW(TAG, "HTTP %d, herverbinden...", status);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(HTTP_RECONNECT_DELAY_MS));
            continue;
        }

        /* Stream lus */
        while (!s_stop_request) {
            int n = esp_http_client_read(client, (char *)buf, HTTP_READ_BUF_SIZE);
            if (n < 0) {
                ESP_LOGW(TAG, "Stream leesfout (%d), herverbinden...", n);
                break;
            }
            if (n == 0) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            size_t offset = 0;
            while (offset < (size_t)n && !s_stop_request) {
                size_t sent = xStreamBufferSend(s_stream_buf,
                                                buf + offset,
                                                n - (int)offset,
                                                pdMS_TO_TICKS(500));
                offset += sent;
            }
        }

        s_http_client = NULL;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        /* Gooi oude data weg na verbreking */
        if (!s_stop_request && s_stream_buf) {
            xStreamBufferReset(s_stream_buf);
            ESP_LOGI(TAG, "StreamBuffer geleegd na verbreking");
            vTaskDelay(pdMS_TO_TICKS(HTTP_RECONNECT_DELAY_MS));
        }
    }

    free(buf);
    ESP_LOGI(TAG, "HTTP taak gestopt");
    s_http_task = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* Decoder taak                                                         */
/* ------------------------------------------------------------------ */
static void decoder_task(void *pvParam)
{
    esp_audio_simple_dec_type_t dec_type =
        (esp_audio_simple_dec_type_t)(intptr_t)pvParam;

    esp_audio_simple_dec_cfg_t dec_cfg = { .dec_type = dec_type };
    esp_audio_simple_dec_handle_t dec = NULL;
    esp_audio_err_t aerr = esp_audio_simple_dec_open(&dec_cfg, &dec);
    if (aerr != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Decoder open mislukt: %d", aerr);
        s_dec_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint8_t *in_buf  = malloc(DEC_IN_BUF_SIZE);
    uint8_t *out_buf = malloc(DEC_OUT_BUF_SIZE);
    if (!in_buf || !out_buf) {
        ESP_LOGE(TAG, "Decoder buf alloc mislukt");
        free(in_buf); free(out_buf);
        esp_audio_simple_dec_close(dec);
        s_dec_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint32_t in_fill    = 0;
    uint32_t stall_count = 0;

    while (!s_stop_request) {
        size_t space = DEC_IN_BUF_SIZE - in_fill;
        if (space > 0) {
            size_t got = xStreamBufferReceive(s_stream_buf,
                                              in_buf + in_fill,
                                              space,
                                              pdMS_TO_TICKS(200));
            in_fill += (uint32_t)got;
        }

        if (in_fill == 0) continue;

        esp_audio_simple_dec_raw_t raw   = { .buffer = in_buf,  .len = in_fill };
        esp_audio_simple_dec_out_t frame = { .buffer = out_buf, .len = DEC_OUT_BUF_SIZE };

        aerr = esp_audio_simple_dec_process(dec, &raw, &frame);

        if (raw.consumed > 0 && raw.consumed <= in_fill) {
            uint32_t remaining = in_fill - raw.consumed;
            if (remaining > 0) memmove(in_buf, in_buf + raw.consumed, remaining);
            in_fill = remaining;
            stall_count = 0;
        } else if (raw.consumed == 0 && frame.decoded_size == 0) {
            stall_count++;
            if (stall_count > 256) {
                ESP_LOGW(TAG, "Decoder vastgelopen, reset...");
                esp_audio_simple_dec_close(dec);
                esp_audio_simple_dec_open(&dec_cfg, &dec);
                in_fill = 0;
                stall_count = 0;
            } else {
                if (in_fill > 1) memmove(in_buf, in_buf + 1, in_fill - 1);
                in_fill = (in_fill > 0) ? in_fill - 1 : 0;
            }
            continue;
        }

        if (aerr == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            ESP_LOGW(TAG, "PCM buf te klein, needed: %lu", frame.needed_size);
            continue;
        }

        if (frame.decoded_size > 0) {
            esp_audio_simple_dec_info_t info;
            if (esp_audio_simple_dec_get_info(dec, &info) == ESP_AUDIO_ERR_OK
                && info.sample_rate > 0) {
                codec_set_rate(info.sample_rate);
            }
            if (s_codec_rate > 0) {
                esp_codec_dev_write(s_codec, out_buf, (int)frame.decoded_size);
            }
        }
    }

    free(in_buf);
    free(out_buf);
    esp_audio_simple_dec_close(dec);
    ESP_LOGI(TAG, "Decoder taak gestopt");
    s_dec_task = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* stop_tasks                                                           */
/* ------------------------------------------------------------------ */
static void stop_tasks(void)
{
    if (!s_dec_task && !s_http_task) return;

    s_stop_request = true;

    /* Korte wachttijd: taken die niet blokkeren stoppen direct */
    for (int i = 0; i < 50; i++) {
        if (!s_dec_task && !s_http_task) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* Deblokeer een eventueel blokkerende esp_http_client_read() door de
     * verbinding te sluiten. Zo kan de HTTP-taak zelf netjes afsluiten
     * i.p.v. force-killed worden terwijl de socket nog open is bij lwIP. */
    if (s_http_task && s_http_client) {
        esp_http_client_close(s_http_client);
    }

    /* Wacht opnieuw tot de taak zichzelf beëindigt */
    for (int i = 0; i < 300; i++) {
        if (!s_dec_task && !s_http_task) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* Laatste redmiddel: force-kill (socket is al gesloten, dus lwIP is veilig) */
    if (s_dec_task)  { vTaskDelete(s_dec_task);  s_dec_task  = NULL; }
    if (s_http_task) { vTaskDelete(s_http_task); s_http_task = NULL; }
    s_http_client = NULL;

    if (s_stream_buf) {
        vStreamBufferDelete(s_stream_buf);
        s_stream_buf = NULL;
    }

    s_stop_request = false;
}

/* ------------------------------------------------------------------ */
/* start_stream                                                         */
/* ------------------------------------------------------------------ */
static esp_err_t start_stream(void)
{
    ESP_LOGI(TAG, "start_stream() url='%s'", s_current_url);

    if (s_current_url[0] == '\0') {
        ESP_LOGE(TAG, "Lege URL");
        return ESP_ERR_INVALID_STATE;
    }

    size_t free_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Heap grootste blok: %u, nodig: %d", free_largest, STREAM_BUFFER_SIZE);

    s_stream_buf = xStreamBufferCreate(STREAM_BUFFER_SIZE, STREAM_TRIGGER_LEVEL);
    if (!s_stream_buf) {
        ESP_LOGE(TAG, "xStreamBufferCreate mislukt");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t r1 = xTaskCreatePinnedToCore(http_stream_task, "http_stream",
                        10240, (void *)s_current_url,
                        HTTP_TASK_PRIO, &s_http_task, 0);
    ESP_LOGI(TAG, "HTTP taak: %s", r1 == pdPASS ? "OK" : "MISLUKT");

    esp_audio_simple_dec_type_t dec_type = detect_dec_type_from_url(s_current_url);
    BaseType_t r2 = xTaskCreatePinnedToCore(decoder_task, "audio_dec",
                        8192, (void *)(intptr_t)dec_type,
                        DEC_TASK_PRIO, &s_dec_task, 1);
    ESP_LOGI(TAG, "Decoder taak: %s", r2 == pdPASS ? "OK" : "MISLUKT");

    if (r1 != pdPASS || r2 != pdPASS) {
        ESP_LOGE(TAG, "Taak aanmaken mislukt");
        stop_tasks();
        return ESP_FAIL;
    }

    s_status = RADIO_STATUS_PLAYING;
    ESP_LOGI(TAG, "Status: PLAYING");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* radio_player_init                                                    */
/* ------------------------------------------------------------------ */
esp_err_t radio_player_init(void)
{
    esp_err_t ret;

    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port             = ES8388_I2C_PORT,
        .sda_io_num           = ES8388_I2C_SDA_PIN,
        .scl_io_num           = ES8388_I2C_SCL_PIN,
        .clk_source           = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt    = 7,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init mislukt: %s", esp_err_to_name(ret));
        return ret;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr       = ES8388_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) { ESP_LOGE(TAG, "I2C ctrl mislukt"); return ESP_FAIL; }

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8388_codec_cfg_t es_cfg = {
        .ctrl_if     = ctrl_if,
        .gpio_if     = gpio_if,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,
        .master_mode = false,
        .pa_pin      = PA_ENABLE_PIN,
        .pa_reverted = false,
    };
    const audio_codec_if_t *codec_if = es8388_codec_new(&es_cfg);
    if (!codec_if) { ESP_LOGE(TAG, "ES8388 mislukt"); return ESP_FAIL; }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear    = true;
    chan_cfg.dma_desc_num  = I2S_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = I2S_DMA_FRAME_NUM;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk  = I2S_MCLK_PIN,
            .bclk  = I2S_BCLK_PIN,
            .ws    = I2S_LRCLK_PIN,
            .dout  = I2S_DOUT_PIN,
            .din   = I2S_DIN_PIN,
            .invert_flags = { false, false, false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_tx_chan, &std_cfg));

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port      = I2S_NUM_0,
        .tx_handle = s_i2s_tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) { ESP_LOGE(TAG, "I2S data mislukt"); return ESP_FAIL; }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if  = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (!s_codec) { ESP_LOGE(TAG, "Codec dev mislukt"); return ESP_FAIL; }

    gpio_config_t hp_conf = {
        .pin_bit_mask = 1ULL << HEADPHONE_DETECT_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&hp_conf);

    codec_set_rate(44100);

    s_ctrl_sem = xSemaphoreCreateMutex();
    if (!s_ctrl_sem) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Radio player gereed.");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Publieke API                                                         */
/* ------------------------------------------------------------------ */
esp_err_t radio_player_play_url(const char *url)
{
    if (!url || url[0] == '\0') return ESP_ERR_INVALID_ARG;
    stop_tasks();
    strlcpy(s_current_url, url, sizeof(s_current_url));
    return start_stream();
}

void radio_player_stop(void)
{
    if (s_status == RADIO_STATUS_STOPPED) return;
    stop_tasks();
    s_status = RADIO_STATUS_STOPPED;
    ESP_LOGI(TAG, "Gestopt");
}

void radio_player_pause(void)
{
    if (s_status != RADIO_STATUS_PLAYING) return;
    stop_tasks();
    s_status = RADIO_STATUS_PAUSED;
    ESP_LOGI(TAG, "Gepauzeerd");
}

void radio_player_resume(void)
{
    if (s_status != RADIO_STATUS_PAUSED) return;
    start_stream();
}

void radio_player_set_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    s_volume = vol;
    if (s_codec) esp_codec_dev_set_out_vol(s_codec, vol);
    ESP_LOGI(TAG, "Volume: %d%%", vol);
}

radio_status_t radio_player_get_status(void) { return s_status; }
int radio_player_get_volume(void)            { return s_volume; }
