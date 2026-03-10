#!/usr/bin/env python3
"""
UART test tool voor Patnic Wekker-Frontend (Waveshare ESP32-S3)
Gedraagt zich als een LyraT: reageert automatisch op commando's van de Waveshare.

Aansluitwijze (USB-UART adapter):
  Adapter TX  →  Waveshare GPIO16 (UART2 RX)
  Adapter RX  →  Waveshare GPIO15 (UART2 TX)
  GND         →  GND

Protocol: [LEN:1][PAYLOAD:LEN]  — opcode is eerste byte van payload
"""

import serial
import time
import struct
import sys
import threading

PORT     = "COM9"    # <-- pas aan
BAUDRATE = 460800

# Opcodes commando's (Waveshare → LyraT)
CMD_PLAY           = 0x01
CMD_STOP           = 0x02
CMD_PAUSE          = 0x03
CMD_VOLUME         = 0x04
CMD_SET_URL        = 0x05
CMD_REQUEST_STATUS = 0x06
CMD_EQ             = 0x07
CMD_REQUEST_TIME    = 0x0A
CMD_REQUEST_WEATHER = 0x0B

# Opcodes events (LyraT → Waveshare)
EVT_STATUS_REPLY   = 0x06
EVT_AUDIO_LEVEL    = 0x08
EVT_TIME_SYNC      = 0x09
EVT_WEATHER        = 0x0C

CMD_NAMES = {
    CMD_PLAY:            "CMD_PLAY",
    CMD_STOP:            "CMD_STOP",
    CMD_PAUSE:           "CMD_PAUSE",
    CMD_VOLUME:          "CMD_VOLUME",
    CMD_SET_URL:         "CMD_SET_URL",
    CMD_REQUEST_STATUS:  "CMD_REQUEST_STATUS",
    CMD_EQ:              "CMD_EQ",
    CMD_REQUEST_TIME:    "CMD_REQUEST_TIME",
    CMD_REQUEST_WEATHER: "CMD_REQUEST_WEATHER",
}

# Gesimuleerde weerdata
SIM_WEATHER_JSON = '{"temp":15.5,"hum":72}'

STATUS_NAMES = {0x00: "STOPPED", 0x01: "PLAYING", 0x02: "PAUSED", 0xFF: "ERROR"}

# Gesimuleerde LyraT-staat
sim_status = 0x00
sim_volume = 50
sim_url    = ""
print_lock = threading.Lock()


def ts():
    return time.strftime("%H:%M:%S")


def log(msg):
    with print_lock:
        print(msg)


def send_frame(ser, payload: bytes, label: str):
    frame = bytes([len(payload)]) + payload
    ser.write(frame)
    log(f"[{ts()}]  >> TX {label:25s}  {frame.hex(' ').upper()}")


def handle_command(ser, payload: bytes):
    """Verwerk een binnenkomend commando en stuur automatisch het juiste antwoord."""
    global sim_status, sim_volume, sim_url

    if not payload:
        return
    opcode = payload[0]
    name = CMD_NAMES.get(opcode, f"ONBEKEND(0x{opcode:02X})")

    # Log het ontvangen commando
    extra = ""
    if opcode == CMD_VOLUME and len(payload) >= 2:
        sim_volume = payload[1]
        extra = f"  volume={sim_volume}"
    elif opcode == CMD_SET_URL and len(payload) >= 2:
        url_len = payload[1]
        sim_url = payload[2:2 + url_len].decode(errors="replace")
        extra = f"  url={sim_url}"
    elif opcode == CMD_EQ and len(payload) >= 3:
        extra = f"  band={payload[1]}  gain={payload[2]}"

    log(f"[{ts()}]  << RX {name}{extra}")

    # Pas gesimuleerde staat aan
    if opcode == CMD_PLAY:
        sim_status = 0x01
    elif opcode == CMD_STOP:
        sim_status = 0x00
    elif opcode == CMD_PAUSE:
        sim_status = 0x02

    # Automatisch antwoord sturen
    if opcode == CMD_REQUEST_TIME:
        epoch = int(time.time())
        t_str = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(epoch))
        payload_tx = bytes([EVT_TIME_SYNC]) + struct.pack(">I", epoch)
        send_frame(ser, payload_tx, f"EVT_TIME_SYNC ({t_str} UTC)")

    elif opcode == CMD_REQUEST_STATUS:
        s_name = STATUS_NAMES.get(sim_status, "?")
        payload_tx = bytes([EVT_STATUS_REPLY, sim_status, sim_volume])
        send_frame(ser, payload_tx, f"EVT_STATUS_REPLY ({s_name}, vol={sim_volume})")

    elif opcode == CMD_REQUEST_WEATHER:
        payload_tx = bytes([EVT_WEATHER]) + SIM_WEATHER_JSON.encode()
        send_frame(ser, payload_tx, f"EVT_WEATHER ({SIM_WEATHER_JSON})")

    elif opcode in (CMD_PLAY, CMD_STOP, CMD_PAUSE, CMD_VOLUME, CMD_SET_URL):
        # Bevestig met huidige status
        s_name = STATUS_NAMES.get(sim_status, "?")
        payload_tx = bytes([EVT_STATUS_REPLY, sim_status, sim_volume])
        send_frame(ser, payload_tx, f"EVT_STATUS_REPLY ({s_name}, vol={sim_volume})")


def main():
    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=0.5)
    except serial.SerialException as e:
        print(f"Kan poort {PORT} niet openen: {e}")
        sys.exit(1)

    time.sleep(0.3)
    ser.reset_input_buffer()

    print(f"=== Patnic Wekker-Frontend UART Tester ===")
    print(f"Simuleert LyraT op {PORT} @ {BAUDRATE} baud")
    print(f"Wacht op commando's van de Waveshare... (Ctrl+C om te stoppen)\n")

    while True:
        try:
            len_byte = ser.read(1)
            if not len_byte:
                continue
            n = len_byte[0]
            payload = ser.read(n) if n > 0 else b""
            handle_command(ser, payload)
        except KeyboardInterrupt:
            break
        except Exception as e:
            log(f"[{ts()}] Fout: {e}")

    ser.close()
    print("\nVerbinding gesloten.")


if __name__ == "__main__":
    main()
