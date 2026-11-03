#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// mic_capture.h - Mikrofon PDM + Voice Activity Detection
// Nagrywa mowę, wykrywa początek/koniec wypowiedzi, buforuje WAV
// =============================================================================

// Stany modułu mikrofonu
typedef enum {
    MIC_IDLE     = 0,   // czeka na aktywność głosową
    MIC_DETECTING,      // wykryto aktywność, nagrywanie w toku
    MIC_READY           // nagranie gotowe do odczytu (cisza wykryta)
} MicState;

/**
 * Inicjalizuje mikrofon PDM (piny z config.h: PDM_CLK_PIN, PDM_DATA_PIN).
 * Alokuje bufor audio w PSRAM.
 * @return true jeśli inicjalizacja powiodła się
 */
bool mic_init();

/**
 * Musi być wywoływane w loop() - odczytuje próbki PDM, przetwarza VAD.
 * Nieblokujące.
 */
void mic_update();

/**
 * Zwraca aktualny stan modułu.
 */
MicState mic_get_state();

/**
 * Sprawdza czy nagranie jest gotowe do odbioru.
 * Równoważne: mic_get_state() == MIC_READY
 */
bool mic_is_ready();

/**
 * Pobiera wskaźnik na bufor WAV z nagraniem.
 * Bufor zawiera pełny nagłówek RIFF WAV + dane PCM 16kHz mono 16-bit.
 * Ważny tylko gdy mic_is_ready() == true.
 * @param out_len  wyjście: rozmiar bufora w bajtach
 * @return wskaźnik na bufor WAV lub NULL
 */
const uint8_t* mic_get_wav(size_t* out_len);

/**
 * Resetuje moduł po odebraniu nagrania, wraca do stanu IDLE.
 * Wywoływane przez state machine po wysłaniu audio do backendu.
 */
void mic_reset();

/**
 * Opróżnia bufor DMA I2S — wywoływać gdy mikrofon nie nagrywa
 * (podczas SM_SENDING, SM_PLAYING) żeby zapobiec przepełnieniu kolejki DMA.
 * Nie zapisuje danych, tylko odczytuje i odrzuca.
 */
void mic_drain();

/**
 * Zwraca aktualny poziom RMS sygnału (do debug/wizualizacji).
 */
uint16_t mic_get_rms();
