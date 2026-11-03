#include "state_machine.h"
#include "mic_capture.h"
#include "audio_player.h"
#include "wifi_transport.h"
#include "nxt_bluetooth.h"
#include "config.h"

#include <Arduino.h>
#include <stdlib.h>

// =============================================================================
// state_machine.cpp - Główna logika przepływu danych ROVER
//
// Przepływ główny:
//   IDLE -> (VAD) -> LISTENING -> (cisza) -> SENDING -> (odpowiedź) -> PLAYING
//   PLAYING -> (ruch w odpowiedzi) -> MOVING -> IDLE
//
// ESP32 jest CIENKIM KLIENTEM:
//   - Zbiera audio i wysyła do backendu
//   - Odbiera WAV TTS i odtwarza
//   - Wykonuje komendy ruchu z endpointu /cmd (backend lub Web UI)
//   - Logika agentowa jest w całości na backendzie (Python + Claude)
// =============================================================================

static SmState _state = SM_IDLE;
static const char* _error_msg = nullptr;

// Bufor odpowiedzi WAV z backendu
static uint8_t* _response_buf     = nullptr;
static size_t   _response_buf_len = 0;

// Nazwy stanów (do logowania i /status)
static const char* STATE_NAMES[] = {
    "IDLE",
    "LISTENING",
    "SENDING",
    "PLAYING",
    "MOVING",
    "ERROR"
};

// -----------------------------------------------------------------------------
// Przejście między stanami
// -----------------------------------------------------------------------------
static void _enter_state(SmState new_state) {
    if (_state == new_state) return;

    DEBUG_PRINTF("[SM] %s -> %s\n", STATE_NAMES[_state], STATE_NAMES[new_state]);
    _state = new_state;

    switch (_state) {
        case SM_IDLE:
            mic_reset();  // Reset mikrofonu - gotowy na nowe nagranie
            break;

        case SM_LISTENING:
            // mic_capture już nagrywał - nic do zrobienia
            DEBUG_PRINTLN("[SM] Listening...");
            break;

        case SM_SENDING:
            DEBUG_PRINTLN("[SM] Sending audio to backend...");
            break;

        case SM_PLAYING:
            DEBUG_PRINTLN("[SM] Playing response...");
            break;

        case SM_MOVING:
            DEBUG_PRINTLN("[SM] Moving...");
            break;

        case SM_ERROR:
            DEBUG_PRINTF("[SM] ERROR: %s\n", _error_msg ? _error_msg : "unknown");
            nxt_stop();  // Zatrzymaj robota przy błędzie
            break;
    }
}

// -----------------------------------------------------------------------------
// Inicjalizacja
// -----------------------------------------------------------------------------
void sm_init() {
    _state = SM_IDLE;
    _error_msg = nullptr;
    _response_buf = nullptr;
    _response_buf_len = 0;
    DEBUG_PRINTLN("[SM] State machine initialized - IDLE");
}

// -----------------------------------------------------------------------------
// Główna pętla maszyny stanów
// -----------------------------------------------------------------------------
void sm_update() {
    // Zawsze aktualizuj timer auto-stop NXT
    nxt_update();

    switch (_state) {

        // -----------------------------------------------------------------------
        case SM_IDLE:
            // Czekamy na wykrycie głosu przez VAD
            mic_update();  // odczytuje próbki PDM, przetwarza VAD

            if (mic_get_state() == MIC_DETECTING) {
                _enter_state(SM_LISTENING);
            }
            break;

        // -----------------------------------------------------------------------
        case SM_LISTENING:
            // Nagrywanie w toku - mic_update przetwarza sygnał
            mic_update();

            if (mic_is_ready()) {
                // Cisza wykryta - nagranie gotowe
                _enter_state(SM_SENDING);
            }
            break;

        // -----------------------------------------------------------------------
        case SM_SENDING:
            {
                // Pobierz bufor WAV z mikrofonu
                size_t wav_len = 0;
                const uint8_t* wav_data = mic_get_wav(&wav_len);

                if (!wav_data || wav_len == 0) {
                    DEBUG_PRINTLN("[SM] No WAV data - back to IDLE");
                    _enter_state(SM_IDLE);
                    break;
                }

                // Zwolnij poprzedni bufor WAV jeśli istnieje
                if (_response_buf) {
                    free(_response_buf);
                    _response_buf = nullptr;
                    _response_buf_len = 0;
                }

                // Wyślij WAV do backendu, odbierz WAV z odpowiedzią TTS
                TransportResult result = transport_post_audio(
                    wav_data, wav_len,
                    &_response_buf, &_response_buf_len
                );

                // Opróżnij bufor DMA mikrofonu po HTTP (przez SM_SENDING mogło
                // nazbierać się dużo danych — przepełnienie powoduje gubienie próbek)
                mic_drain();

                if (result == TRANSPORT_OK && _response_buf && _response_buf_len > 0) {
                    _enter_state(SM_PLAYING);
                } else if (result == TRANSPORT_NO_CONTENT) {
                    // Pusty transkrypt (cisza) — wróć cicho do IDLE
                    DEBUG_PRINTLN("[SM] No speech detected - back to IDLE");
                    _enter_state(SM_IDLE);
                } else {
                    DEBUG_PRINTF("[SM] Transport failed: %d - back to IDLE\n", result);
                    _enter_state(SM_IDLE);
                }
            }
            break;

        // -----------------------------------------------------------------------
        case SM_PLAYING:
            {
                // Odtwórz WAV (blokujące) — opróżnij DMA przed odtworzeniem
                mic_drain();
                if (_response_buf && _response_buf_len > 0) {
                    audio_play_wav(_response_buf, _response_buf_len);
                    free(_response_buf);
                    _response_buf = nullptr;
                    _response_buf_len = 0;
                }
                // Opróżnij DMA po odtworzeniu (nazbierało się podczas audio_play_wav)
                mic_drain();
                // Po odtworzeniu wracamy do IDLE
                _enter_state(SM_IDLE);
            }
            break;

        // -----------------------------------------------------------------------
        case SM_MOVING:
            // Ruch jest obsługiwany przez nxt_update() (auto-stop)
            // oraz przez /cmd endpoint (web_server_esp.cpp)
            // SM_MOVING jest ustawiany zewnętrznie przez /cmd handler
            if (!nxt_is_connected()) {
                _enter_state(SM_IDLE);
            }
            break;

        // -----------------------------------------------------------------------
        case SM_ERROR:
            // W stanie błędu czekamy - reset przez Serial lub restart
            delay(1000);
            break;
    }
}

// -----------------------------------------------------------------------------
// API publiczne
// -----------------------------------------------------------------------------
const char* sm_state_name() {
    return STATE_NAMES[_state];
}

SmState sm_get_state() {
    return _state;
}

void sm_error(const char* message) {
    _error_msg = message;
    _enter_state(SM_ERROR);
}
