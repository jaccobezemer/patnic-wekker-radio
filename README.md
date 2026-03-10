# Patnic Wekker-Radio

Monorepo voor de Patnic internetradio/wekker, bestaande uit een audio backend en een touchscreen frontend.

---

## Hardware

### Backend — ESP32-LyraT v4.3

| Component  | Details                                                |
|------------|--------------------------------------------------------|
| Board      | ESP32-LyraT v4.3                                       |
| Codec      | ES8388 (I²C + I²S)                                     |
| Versterker | NS4150 BTL via GPIO 21                                 |
| I²C        | SDA GPIO 18, SCL GPIO 23                               |
| I²S        | MCLK GPIO 0, BCLK GPIO 5, LRCLK GPIO 25, DOUT GPIO 26  |
| UART2      | TX GPIO 15 → frontend RX, RX GPIO 16 ← frontend TX     |

### Frontend — Waveshare ESP32-S3-Touch-LCD-7B

| Component        | Details                                           |
|------------------|---------------------------------------------------|
| Board            | Waveshare ESP32-S3-Touch-LCD-7B                   |
| LCD Controller   | ST7262                                            |
| Touch Controller | GT911                                             |
| Touch I²C        | SDA GPIO 8, SCL GPIO 9                            |
| UART2            | RX GPIO 16 ← backend TX, TX GPIO 15 → backend RX  |

---

## Architectuur

```text
Frontend (Waveshare ESP32-S3)          Backend (ESP32-LyraT)
┌────────────────────────────┐         ┌────────────────────────────┐
│  LVGL UI                   │         │  HTTP streaming            │
│  lyrat_ctrl (UART driver)  │◄──────► │  uart_ctrl (UART driver)   │
│  WiFi (NTP + weer display) │  UART   │  WiFi + captive portal     │
└────────────────────────────┘         │  Weerdata (Open-Meteo)     │
                                       └────────────────────────────┘
```

- **Frontend** stuurt commando's (play, stop, URL, volume, ...) naar de backend
- **Backend** speelt audio af via ES8388 codec en stuurt statusberichten terug
- **Backend** haalt periodiek weerdata op via [Open-Meteo](https://open-meteo.com/) (geen API key nodig) en stuurt deze op verzoek door naar de frontend
- **Backend** beantwoordt tijdverzoeken met de huidige UTC epoch (na NTP sync)

---

## Software

### Backend
- **ESP-IDF 5.x**
- **espressif/esp_codec_dev** — ES8388 hardware codec aansturing
- **espressif/esp_audio_codec** — MP3/AAC software decoder
- HTTP streaming via `esp_http_client`
- Weerdata via Open-Meteo API (cJSON parser)

### Frontend

- **ESP-IDF 5.x**
- **LVGL** — UI framework
- **esp_lcd** — RGB display aansturing

### Gedeeld

- **wekker_protocol** (`components/wekker_protocol/`) — gezaghebbende UART protocol definities, gebruikt door zowel backend als frontend

---

## Projectstructuur

```text
patnic-wekker-radio/
├── backend/                          # ESP32-LyraT v4.3
│   ├── main/
│   │   ├── main.c                    # Toegangspunt + UART callbacks
│   │   ├── radio_player.c/h          # HTTP streaming + MP3/AAC + ES8388
│   │   ├── uart_ctrl.c/h             # UART communicatie met frontend
│   │   ├── wifi_manager.c/h          # WiFi verbinding + DHCP hostname
│   │   ├── captive_portal.c/h        # Captive portal voor WiFi configuratie
│   │   ├── nvs_settings.c/h          # Instellingen opslaan in NVS flash
│   │   ├── weather.c/h               # Open-Meteo weerdata (periodiek + on-demand)
│   │   ├── Kconfig.projbuild         # Menuconfig opties
│   │   └── idf_component.yml         # Managed component dependencies
│   ├── uart_tester.py                # PC testscript voor backend UART protocol
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   └── partitions.csv                # Custom partitietabel (2 MB flash)
├── frontend/                         # Waveshare ESP32-S3-Touch-LCD-7B
│   ├── main/
│   │   ├── main.c                    # Toegangspunt + UI logica
│   │   └── idf_component.yml         # Managed component dependencies
│   ├── components/
│   │   └── lyrat_ctrl/               # UART driver richting backend
│   │       ├── lyrat_ctrl.c/h
│   │       └── CMakeLists.txt
│   ├── uart_tester.py                # PC testscript dat LyraT simuleert
│   ├── CMakeLists.txt
│   └── sdkconfig.defaults
├── components/
│   └── wekker_protocol/              # Gedeeld UART protocol (gezaghebbend)
│       ├── wekker_protocol.h
│       ├── wekker_protocol.c
│       └── CMakeLists.txt
├── patnic-wekker-radio.code-workspace
├── .gitignore
└── README.md
```

---

## Configuratie (menuconfig)

### Backend — `idf.py menuconfig` → **Wekker-Radio Configuratie**

| Optie               | Standaard             | Beschrijving                                    |
|---------------------|-----------------------|-------------------------------------------------|
| `DEVICE_HOSTNAME`   | `patnic-wekker-radio` | DHCP hostnaam (zichtbaar in router)             |
| `SOFTAP_SSID`       | `patnic-wekker-radio` | Naam van het captive portal WiFi-netwerk        |
| `DEFAULT_VOLUME`    | `10`                  | Startvolume (0–100)                             |
| `UART_BAUDRATE`     | `460800`              | Baudrate UART2 (moet overeenkomen met frontend) |
| `WEATHER_LATITUDE`  | `51.6417`             | Breedtegraad voor weerdata                      |
| `WEATHER_LONGITUDE` | `4.8611`              | Lengtegraad voor weerdata                       |

WiFi SSID en wachtwoord worden ingesteld via de **captive portal** — geen menuconfig nodig. De backend is bereikbaar als `patnic-wekker-radio` in de DHCP leases van de router.

Radiostation-URL's worden **altijd door de frontend gepushed** via `CMD_SET_URL`. Er zijn geen vaste stations in de backend geconfigureerd.

### Frontend — `idf.py menuconfig` → **Wekker-Radio Configuratie**

| Optie                 | Standaard | Beschrijving                                                      |
|-----------------------|-----------|-------------------------------------------------------------------|
| `LYRAT_UART_BAUDRATE` | `460800`  | Baudrate UART2 (moet overeenkomen met backend)                    |
| `LYRAT_UART_TX_PIN`   | `15`      | GPIO Waveshare TX → LyraT RX                                      |
| `LYRAT_UART_RX_PIN`   | `16`      | GPIO LyraT TX → Waveshare RX                                      |
| `DEFAULT_VOLUME`      | `10`      | Startvolume dat bij opstart naar de LyraT wordt gestuurd (0–100)  |
| `DEFAULT_BRIGHTNESS`  | `80`      | Helderheid LCD-backlight bij opstarten (0–100)                    |

---

## UART Protocol

Communicatie via UART2 (460800 baud, instelbaar via menuconfig) tussen frontend en backend, length-prefixed frames:

```text
[LEN: 1 byte][PAYLOAD: LEN bytes]
```

Opcode is de eerste byte van de payload. Gedefinieerd in `components/wekker_protocol/wekker_protocol.h`.

### Commando's (frontend → backend)

| Opcode | Naam                  | Payload                                      |
|--------|-----------------------|----------------------------------------------|
| `0x01` | `CMD_PLAY`            | —                                            |
| `0x02` | `CMD_STOP`            | —                                            |
| `0x03` | `CMD_PAUSE`           | —                                            |
| `0x04` | `CMD_VOLUME`          | `[vol: uint8, 0–100]`                        |
| `0x05` | `CMD_SET_URL`         | `[url_len: uint8][url: url_len bytes]`        |
| `0x06` | `CMD_STATUS`          | —                                            |
| `0x07` | `CMD_EQ`              | `[band: uint8, 0–9][gain: int8, dB]`         |
| `0x0A` | `CMD_REQUEST_TIME`    | —                                            |
| `0x0B` | `CMD_REQUEST_WEATHER` | —                                            |

### Antwoorden (backend → frontend)

| Opcode | Naam               | Payload                                                      |
|--------|--------------------|--------------------------------------------------------------|
| `0x06` | `REPLY_STATUS`     | `[status: uint8][volume: uint8]`                             |
| `0x08` | `REPLY_AUDIO_LEVEL`| `[peak_L: uint8][peak_R: uint8]`                             |
| `0x09` | `REPLY_TIME_SYNC`  | `[epoch: uint32, big-endian]`                                |
| `0x0C` | `REPLY_WEATHER`    | `[json: n bytes]` — bijv. `{"temp":12.5,"hum":82}`           |

### Status waarden (in `REPLY_STATUS`)

| Waarde | Betekenis |
|--------|-----------|
| `0x00` | STOPPED   |
| `0x01` | PLAYING   |
| `0x02` | PAUSED    |
| `0xFF` | ERROR     |

---

## UART Testscripts

Beide subprojecten bevatten een Python testscript voor gebruik met een USB-UART adapter.

### `backend/uart_tester.py`

Verstuurt commando's naar de backend en toont antwoorden. Interactief menu met alle ondersteunde commando's inclusief `CMD_REQUEST_WEATHER`.

```bash
# Pas PORT aan bovenin het script
python backend/uart_tester.py
```

### `frontend/uart_tester.py`

Simuleert de LyraT (backend). Reageert automatisch op binnenkomende commando's van de Waveshare frontend:

- `CMD_REQUEST_TIME` → antwoord met huidige UTC epoch
- `CMD_REQUEST_STATUS` → antwoord met gesimuleerde status/volume
- `CMD_REQUEST_WEATHER` → antwoord met gesimuleerde weerdata `{"temp":15.5,"hum":72}`
- `CMD_PLAY/STOP/PAUSE/VOLUME/SET_URL` → bevestig met `REPLY_STATUS`

```bash
python frontend/uart_tester.py
```

**Aansluiting USB-UART adapter voor frontend tester:**

```text
Adapter TX  →  Waveshare GPIO 16 (UART2 RX)
Adapter RX  →  Waveshare GPIO 15 (UART2 TX)
GND         →  GND
```

---

## VSCode

Open het project als multi-root workspace:

1. **File → Open Workspace from File**
2. Selecteer `patnic-wekker-radio.code-workspace`

De workspace toont drie folders: `patnic-wekker-radio` (root), `Backend (ESP32-LyraT)` en `Frontend (ESP32-S3)`. De ESP-IDF GUI knoppen (build, flash, monitor) werken per subproject.

---

## Bouwen en flashen

### Via VSCode
1. Open de workspace via `patnic-wekker-radio.code-workspace`
2. Selecteer het gewenste subproject in de ESP-IDF extensie
3. Gebruik de GUI knoppen: **Build**, **Flash**, **Monitor**

### Via terminal

```bash
# Backend (ESP32-LyraT)
cd backend
idf.py set-target esp32
idf.py menuconfig          # optioneel: hostname, SSID, volume, locatie instellen
idf.py flash monitor

# Frontend (ESP32-S3)
cd frontend
idf.py set-target esp32s3
idf.py flash monitor
```

> **Backend flash:** 2 MB. Custom partitietabel geeft 1,75 MB aan de applicatie.
