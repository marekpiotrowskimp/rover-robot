#include "wifi_transport.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <stdlib.h>

// =============================================================================
// wifi_transport.cpp - Komunikacja HTTP z backendem Python
//
// Backend Python nasłuchuje na BACKEND_HOST:BACKEND_PORT (config.h)
// Endpointy backendu:
//   POST /api/speak  <- audio WAV (nagranie głosu) -> odpowiedź WAV (TTS)
//   POST /api/event  <- zdarzenia ESP32 -> odpowiedź JSON
// =============================================================================

bool transport_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

// -----------------------------------------------------------------------------
// POST /api/speak - wysyła WAV (nagranie głosu), odbiera WAV (odpowiedź TTS)
// -----------------------------------------------------------------------------
TransportResult transport_post_audio(
    const uint8_t* wav_data, size_t wav_len,
    uint8_t** out_buf, size_t* out_buf_len)
{
    if (!transport_is_connected()) {
        DEBUG_PRINTLN("[HTTP] No WiFi");
        return TRANSPORT_ERR_WIFI;
    }

    *out_buf     = nullptr;
    *out_buf_len = 0;

    HTTPClient http;
    http.begin(BACKEND_SPEAK_URL);
    http.setTimeout(SM_HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "audio/wav");
    http.addHeader("Content-Length", String(wav_len));

    DEBUG_PRINTF("[HTTP] POST %s (%d bytes WAV)\n", BACKEND_SPEAK_URL, (int)wav_len);

    int code = http.POST((uint8_t*)wav_data, wav_len);

    if (code <= 0) {
        DEBUG_PRINTF("[HTTP] Connection error: %d\n", code);
        http.end();
        return TRANSPORT_ERR_TIMEOUT;
    }

    // 204 = pusty transkrypt (cisza/szum) — wróć cicho do IDLE
    if (code == 204) {
        DEBUG_PRINTLN("[HTTP] 204 No Content — empty transcript, ignoring");
        http.end();
        return TRANSPORT_NO_CONTENT;
    }

    if (code != 200) {
        DEBUG_PRINTF("[HTTP] Server error: HTTP %d\n", code);
        http.end();
        return TRANSPORT_ERR_HTTP;
    }

    // Odbierz odpowiedź WAV (TTS)
    int content_len = http.getSize();
    DEBUG_PRINTF("[HTTP] Response: %d bytes WAV\n", content_len);

    if (content_len <= 0 || content_len > 512 * 1024) {
        // content_len == -1 gdy chunked transfer, fallback: pobierz cały stream
        // Dla bezpieczeństwa ograniczamy do 512 KB
        DEBUG_PRINTLN("[HTTP] Streaming response (no Content-Length)");
        content_len = 128 * 1024;  // domyślny bufor 128 KB
    }

    uint8_t* resp_buf = (uint8_t*)ps_malloc(content_len);
    if (!resp_buf) {
        // Fallback na heap standardowy
        resp_buf = (uint8_t*)malloc(content_len);
    }
    if (!resp_buf) {
        DEBUG_PRINTLN("[HTTP] No memory for WAV buffer");
        http.end();
        return TRANSPORT_ERR_NO_MEM;
    }

    // Odczytaj strumień odpowiedzi
    WiFiClient* stream = http.getStreamPtr();
    size_t bytes_read = 0;
    unsigned long timeout_ms = millis() + SM_HTTP_TIMEOUT_MS;

    while (bytes_read < (size_t)content_len && millis() < timeout_ms) {
        if (stream->available()) {
            size_t chunk = stream->readBytes(
                resp_buf + bytes_read,
                min((size_t)(content_len - bytes_read), (size_t)1024)
            );
            bytes_read += chunk;
        } else {
            delay(1);
        }
    }

    http.end();

    if (bytes_read == 0) {
        free(resp_buf);
        DEBUG_PRINTLN("[HTTP] Empty WAV response");
        return TRANSPORT_ERR_HTTP;
    }

    *out_buf     = resp_buf;
    *out_buf_len = bytes_read;

    DEBUG_PRINTF("[HTTP] WAV received: %d bytes\n", (int)bytes_read);
    return TRANSPORT_OK;
}

// -----------------------------------------------------------------------------
// POST /api/event - wysyła zdarzenie JSON
// -----------------------------------------------------------------------------
TransportResult transport_post_event(const char* event_type, const char* json_data) {
    if (!transport_is_connected()) {
        return TRANSPORT_ERR_WIFI;
    }

    // Zbuduj JSON body
    char body[256];
    if (json_data && strlen(json_data) > 0) {
        snprintf(body, sizeof(body),
                 "{\"type\":\"%s\",\"data\":%s}",
                 event_type, json_data);
    } else {
        snprintf(body, sizeof(body),
                 "{\"type\":\"%s\"}",
                 event_type);
    }

    HTTPClient http;
    http.begin(BACKEND_EVENT_URL);
    http.setTimeout(SM_HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");

    DEBUG_PRINTF("[HTTP] POST event: %s\n", body);

    int code = http.POST((uint8_t*)body, strlen(body));
    http.end();

    if (code <= 0) return TRANSPORT_ERR_TIMEOUT;
    if (code != 200) return TRANSPORT_ERR_HTTP;

    return TRANSPORT_OK;
}
