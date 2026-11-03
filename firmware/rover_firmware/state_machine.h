#pragma once

// =============================================================================
// state_machine.h - Maszyna stanów ROVER
//
// Stany:
//   SM_IDLE      - czeka na aktywność głosową (VAD)
//   SM_LISTENING - VAD wykrył mowę, mic_capture nagrywa
//   SM_SENDING   - nagranie gotowe, wysyłanie WAV do backendu
//   SM_PLAYING   - odtwarzanie odpowiedzi MP3 przez I2S
//   SM_MOVING    - robot wykonuje komendę ruchu (może być równolegle z PLAYING)
//   SM_ERROR     - błąd krytyczny, wymaga restartu
// =============================================================================

typedef enum {
    SM_IDLE      = 0,
    SM_LISTENING,
    SM_SENDING,
    SM_PLAYING,
    SM_MOVING,
    SM_ERROR
} SmState;

/**
 * Inicjalizuje maszynę stanów. Wywołaj po inicjalizacji wszystkich modułów.
 */
void sm_init();

/**
 * Główna pętla maszyny stanów. Wywoływać w loop() Arduino.
 */
void sm_update();

/**
 * Zwraca aktualny stan jako string (do logowania i /status).
 */
const char* sm_state_name();

/**
 * Zwraca aktualny stan jako enum.
 */
SmState sm_get_state();

/**
 * Wymusza przejście do stanu SM_ERROR z komunikatem.
 * Używane przez moduły przy błędach krytycznych.
 */
void sm_error(const char* message);
