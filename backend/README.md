# Patnic Wekker-Radio

Internetradio voor de **ESP32-LyraT v4.3**. Streamt MP3- en AAC-radiostations via WiFi en speelt ze af via de ingebouwde ES8388 audiocodec. Aangestuurd via UART vanuit een Waveshare display.

## Hardware

| Component | Details |
|-----------|---------|
| Board | ESP32-LyraT v4.3 |
| Codec | ES8388 (I²C + I²S) |
| Versterker | NS4150 BTL via GPIO 21 |
| I²C | SDA GPIO 18, SCL GPIO 23 |
| I²S | MCLK GPIO 0, BCLK GPIO 5, LRCLK GPIO 25, DOUT GPIO 26 |
| UART | UART2 (display communicatie) |

## Software

- **ESP-IDF 5.x** (geen ESP-ADF vereist)
- **espressif/esp_codec_dev** — ES8388 hardware codec aansturing
- **espressif/esp_audio_codec** — MP3/AAC software decoder
- HTTP streaming via `esp_http_client`

## Configuratie

Via `idf.py menuconfig` onder **Wekker-Radio Configuratie**:

- WiFi SSID en wachtwoord
- Tot 8 radiostations (naam + stream-URL)
- Standaard volume (0–100)

## Bouwen en flashen

```bash
idf.py build flash monitor
```

> **Flash:** 2 MB. Custom partitietabel geeft 1,75 MB aan de applicatie.

## Projectstructuur

```
main/
  main.c               # Toegangspunt
  radio_player.c/h     # HTTP streaming + MP3/AAC decodering + ES8388 init
  uart_ctrl.c/h        # UART communicatie met Waveshare display
  wifi_manager.c/h     # WiFi verbinding + captive portal
  captive_portal.c/h   # Captive portal voor WiFi configuratie
  nvs_settings.c/h     # Instellingen opslaan in NVS flash
  Kconfig.projbuild    # Menuconfig opties
  idf_component.yml    # Managed component dependencies
managed_components/
  espressif__esp_codec_dev/   # ES8388 codec driver
  espressif__esp_audio_codec/ # MP3/AAC decoder
partitions.csv         # Custom partitietabel (2 MB flash)
sdkconfig.defaults     # Standaardinstellingen
uart_tester.py         # PC-tool voor UART protocol testen (Python)
```

## UART protocol

Communicatie met het Waveshare display via length-prefixed frames:
`[LEN:1 byte][PAYLOAD:LEN bytes]`

Ondersteunde commando's: play, stop, pause, volume, set_url, status_request.
