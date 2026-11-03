#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// wifi_transport.h - Komunikacja HTTP z backendem Python (FastAPI)
// =============================================================================

// Kody wyniku operacji HTTP
typedef enum {
    TRANSPORT_OK            = 0,
    TRANSPORT_NO_CONTENT    = 1,    // HTTP 204 — brak odpowiedzi (pusty transkrypt)
    TRANSPORT_ERR_WIFI      = -1,   // brak WiFi
    TRANSPORT_ERR_TIMEOUT   = -2,   // timeout połączenia
    TRANSPORT_ERR_HTTP      = -3,   // błąd HTTP (4xx, 5xx)
    TRANSPORT_ERR_NO_MEM    = -4,   // brak pamięci na odpowiedź
} TransportResult;

/**
 * Wysyła nagranie audio WAV do backendu (POST /api/speak).
 * Backend przetwarza: WAV -> Whisper STT -> Agent -> TTS -> WAV
 *
 * @param wav_data    wskaźnik na dane WAV (nagranie głosu)
 * @param wav_len     rozmiar w bajtach
 * @param out_buf     wyjście: bufor z odpowiedzią WAV TTS (alokowany przez malloc, zwolnij po użyciu)
 * @param out_buf_len wyjście: rozmiar odpowiedzi WAV
 * @return TRANSPORT_OK lub kod błędu
 */
TransportResult transport_post_audio(
    const uint8_t* wav_data, size_t wav_len,
    uint8_t** out_buf, size_t* out_buf_len
);

/**
 * Wysyła zdarzenie/event do backendu (POST /api/event).
 * Używane do powiadamiania backendu o zdarzeniach ESP32.
 *
 * @param event_type  typ zdarzenia np. "motion", "boot", "error"
 * @param json_data   opcjonalne dane JSON (może być NULL)
 * @return TRANSPORT_OK lub kod błędu
 */
TransportResult transport_post_event(const char* event_type, const char* json_data);

/**
 * Sprawdza czy WiFi jest połączone.
 */
bool transport_is_connected();
