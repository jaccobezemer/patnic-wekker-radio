#!/usr/bin/env python3
"""
UART test tool voor Patnic Wekker-Radio (LyraT v4.3)
Protocol: [LEN:1][PAYLOAD:LEN]  — opcode is eerste byte van payload
"""

import serial
import time
import struct
import sys

PORT     = "COM9"    # <-- pas aan
BAUDRATE = 460800

# Opcodes uitgestuurde commando's (test ESP32 → LyraT)
CMD_PLAY           = 0x01
CMD_STOP           = 0x02
CMD_PAUSE          = 0x03
CMD_VOLUME         = 0x04
CMD_SET_URL        = 0x05
CMD_STATUS         = 0x06
CMD_EQ             = 0x07
CMD_REQUEST_TIME    = 0x0A
CMD_REQUEST_WEATHER = 0x0B

# Opcodes antwoorden van LyraT (LyraT → test ESP32)
REPLY_STATUS       = 0x06   # [0x06][status][volume]
REPLY_AUDIO_LEVEL  = 0x08   # [0x08][peak_L][peak_R]
REPLY_TIME_SYNC    = 0x09   # [0x09][b3][b2][b1][b0] UTC epoch
REPLY_WEATHER      = 0x0C   # [0x0C][json bytes...]

STATIONS = [
    ("NPO Radio 2 (mp3)", "http://icecast.omroep.nl/radio2-bb-mp3"),
    ("NPO Radio 5 (aac)", "http://icecast.omroep.nl/radio5-bb-aac"),
    ("Radio 538 (aac)",   "http://playerservices.streamtheworld.com/api/livestream-redirect/RADIO538AAC.aac"),
    ("Qmusic (mp3)",      "http://stream.qmusic.nl/qmusic/mp3"),
]

STATUS_NAMES = {
    0x00: "STOPPED",
    0x01: "PLAYING",
    0x02: "PAUSED",
    0xFF: "ERROR",
}


def send_frame(ser: serial.Serial, payload: bytes):
    frame = bytes([len(payload)]) + payload
    ser.write(frame)
    print(f"  >> Verstuurd: {frame.hex(' ').upper()}")


def read_frame(ser: serial.Serial, timeout=2.0):
    ser.timeout = timeout
    len_byte = ser.read(1)
    if not len_byte:
        print("  << Timeout: geen antwoord ontvangen")
        return None
    n = len_byte[0]
    payload = ser.read(n)
    print(f"  << Ontvangen: {(len_byte + payload).hex(' ').upper()}")
    return payload


def read_frame_expect(ser: serial.Serial, expected_opcode: int, timeout=3.0):
    """Leest frames totdat het verwachte opcode binnenkomt of timeout verstrijkt.
    Tussenliggende frames (bijv. spontane STATUS_REPLY) worden getoond maar genegeerd."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        payload = read_frame(ser, timeout=remaining)
        if payload is None:
            break
        if payload[0] == expected_opcode:
            return payload
        # Verkeerde opcode — toon en sla over
        print(f"      (spontaan frame, wachten op 0x{expected_opcode:02X}...)")
        decode_reply(payload)
    return None


def decode_reply(payload: bytes):
    if not payload:
        return
    opcode = payload[0]
    if opcode == REPLY_STATUS and len(payload) >= 3:
        status = STATUS_NAMES.get(payload[1], f"0x{payload[1]:02X}")
        volume = payload[2]
        print(f"      STATUS_REPLY: status={status}, volume={volume}")
    elif opcode == REPLY_TIME_SYNC and len(payload) >= 5:
        epoch = struct.unpack(">I", payload[1:5])[0]
        t = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(epoch))
        print(f"      TIME_SYNC: epoch={epoch} ({t} UTC)")
    elif opcode == REPLY_AUDIO_LEVEL and len(payload) >= 3:
        print(f"      AUDIO_LEVEL: L={payload[1]}, R={payload[2]}")
    elif opcode == REPLY_WEATHER and len(payload) >= 2:
        json_str = payload[1:].decode(errors="replace")
        print(f"      WEATHER: {json_str}")
    else:
        print(f"      Onbekend antwoord opcode: 0x{opcode:02X}")


def menu():
    print()
    print("=== Patnic Wekker-Radio UART Tester ===")
    print(f"    Poort: {PORT} @ {BAUDRATE} baud")
    print()
    print("  1. CMD_PLAY           (0x01)")
    print("  2. CMD_STOP           (0x02)")
    print("  3. CMD_PAUSE          (0x03)")
    print("  4. CMD_VOLUME         (0x04) — vraagt om waarde 0-100")
    print("  5. CMD_SET_URL        (0x05) — kies radiostation")
    print("  6. CMD_STATUS         (0x06) — antwoord: REPLY_STATUS")
    print("  7. CMD_EQ             (0x07) — vraagt om band + gain")
    print("  A. CMD_REQUEST_TIME   (0x0A) — antwoord: REPLY_TIME_SYNC")
    print("  B. CMD_REQUEST_WEATHER(0x0B) — antwoord: REPLY_WEATHER")
    print("  Q. Afsluiten")
    print()


def main():
    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=2)
    except serial.SerialException as e:
        print(f"Kan poort {PORT} niet openen: {e}")
        sys.exit(1)

    time.sleep(0.3)
    print(f"Verbonden met {PORT}")
    ser.reset_input_buffer()

    while True:
        menu()
        keuze = input("Keuze: ").strip().upper()

        if keuze == "Q":
            break

        elif keuze == "1":
            print("CMD_PLAY")
            send_frame(ser, bytes([CMD_PLAY]))

        elif keuze == "2":
            print("CMD_STOP")
            send_frame(ser, bytes([CMD_STOP]))

        elif keuze == "3":
            print("CMD_PAUSE")
            send_frame(ser, bytes([CMD_PAUSE]))

        elif keuze == "4":
            vol = int(input("Volume (0-100): ").strip())
            send_frame(ser, bytes([CMD_VOLUME, max(0, min(100, vol))]))

        elif keuze == "5":
            print("Kies radiostation:")
            for i, (naam, _) in enumerate(STATIONS, 1):
                print(f"  {i}. {naam}")
            keuze_station = input("Station (1-4): ").strip()
            try:
                idx = int(keuze_station) - 1
                if not 0 <= idx < len(STATIONS):
                    raise ValueError()
            except ValueError:
                print("Ongeldige keuze")
                continue
            naam, url_str = STATIONS[idx]
            url = url_str.encode()
            print(f"CMD_SET_URL → {naam}: {url_str}")
            send_frame(ser, bytes([CMD_SET_URL, len(url)]) + url)

        elif keuze == "6":
            print("CMD_STATUS")
            send_frame(ser, bytes([CMD_STATUS]))
            reply = read_frame(ser)
            decode_reply(reply)
            continue

        elif keuze == "7":
            band = int(input("Band (0-9): ").strip())
            gain = int(input("Gain (-13 tot +13 dB): ").strip())
            send_frame(ser, bytes([CMD_EQ, band & 0xFF, gain & 0xFF]))

        elif keuze == "A":
            print("CMD_REQUEST_TIME")
            ser.reset_input_buffer()
            send_frame(ser, bytes([CMD_REQUEST_TIME]))
            reply = read_frame(ser)
            decode_reply(reply)
            continue

        elif keuze == "B":
            print("CMD_REQUEST_WEATHER")
            ser.reset_input_buffer()
            send_frame(ser, bytes([CMD_REQUEST_WEATHER]))
            reply = read_frame(ser)
            decode_reply(reply)
            continue

        else:
            print("Onbekende keuze")
            continue

        # Lees optioneel antwoord (bijv. STATUS_REPLY na PLAY/STOP/PAUSE)
        reply = read_frame(ser, timeout=1.0)
        if reply:
            decode_reply(reply)

    ser.close()
    print("Verbinding gesloten.")


if __name__ == "__main__":
    main()
