#pragma once
#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* UART frame formaat: [LEN:1 byte][PAYLOAD:LEN bytes]                 */
/* ------------------------------------------------------------------ */
#define WEKKER_MAX_PAYLOAD      255
#define WEKKER_MAX_URL_LEN      200
#define WEKKER_MAX_WEATHER_LEN  200  /* max JSON payload voor weerdata */

/* ------------------------------------------------------------------ */
/* Commando's: frontend → backend                                       */
/* ------------------------------------------------------------------ */
#define CMD_PLAY            0x01  /* Start / hervat afspelen                    */
#define CMD_STOP            0x02  /* Stop afspelen                              */
#define CMD_PAUSE           0x03  /* Pauzeer afspelen                           */
#define CMD_VOLUME          0x04  /* [vol:uint8 0-100]                          */
#define CMD_SET_URL         0x05  /* [len:uint8][url:len bytes]                 */
#define CMD_STATUS          0x06  /* Vraag huidige status + volume op           */
#define CMD_EQ              0x07  /* [band:uint8 0-9][gain:int8 dB]             */
#define CMD_NEXT            0x08  /* Volgende station (toekomstig gebruik)      */
#define CMD_PREV            0x09  /* Vorig station   (toekomstig gebruik)       */
#define CMD_REQUEST_TIME    0x0A  /* Vraag UTC epoch op                         */
#define CMD_REQUEST_WEATHER 0x0B  /* Vraag weerdata op (JSON antwoord)          */

/* ------------------------------------------------------------------ */
/* Antwoorden: backend → frontend                                       */
/* ------------------------------------------------------------------ */
#define REPLY_STATUS        0x06  /* [status:uint8][volume:uint8]               */
#define REPLY_AUDIO_LEVEL   0x08  /* [peak_L:uint8][peak_R:uint8]               */
#define REPLY_TIME_SYNC     0x09  /* [epoch:uint32 big-endian]                  */
#define REPLY_WEATHER       0x0C  /* [json:n bytes, geen null-terminator]       */

/* ------------------------------------------------------------------ */
/* Status waarden (in REPLY_STATUS)                                    */
/* ------------------------------------------------------------------ */
#define WEKKER_STATUS_STOPPED   0x00
#define WEKKER_STATUS_PLAYING   0x01
#define WEKKER_STATUS_PAUSED    0x02
#define WEKKER_STATUS_ERROR     0xFF

/* ------------------------------------------------------------------ */
/* Frame hulpfuncties                                                   */
/* ------------------------------------------------------------------ */

/* Bouw een frame in buf. Geeft totale frame lengte terug. */
static inline size_t wekker_build_frame(uint8_t *buf, const uint8_t *payload, uint8_t payload_len)
{
    buf[0] = payload_len;
    for (uint8_t i = 0; i < payload_len; i++) buf[1 + i] = payload[i];
    return 1 + (size_t)payload_len;
}
