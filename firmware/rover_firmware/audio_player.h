#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// audio_player.h - Odtwarzanie MP3/WAV przez I2S (Audio BFF Add-on)
// Adafruit 5769 z MAX98357A montowany back-to-back na XIAO ESP32S3
// =============================================================================

/**
 * Inicjalizuje I2S do odtwarzania dźwięku.
 * Piny z config.h: I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN
 * @return true jeśli inicjalizacja powiodła się
 */
bool audio_init();

/**
 * Odtwarza dane MP3 z bufora w pamięci.
 * Blokujące - wraca po zakończeniu odtwarzania.
 * @param data   wskaźnik na dane MP3
 * @param len    rozmiar danych w bajtach
 */
void audio_play_mp3(const uint8_t* data, size_t len);

/**
 * Odtwarza dane WAV (PCM) z bufora w pamięci.
 * Blokujące - wraca po zakończeniu odtwarzania.
 * @param data   wskaźnik na dane WAV (z nagłówkiem RIFF)
 * @param len    rozmiar danych w bajtach
 */
void audio_play_wav(const uint8_t* data, size_t len);

/**
 * Sprawdza czy aktualnie trwa odtwarzanie.
 */
bool audio_is_playing();

/**
 * Natychmiast przerywa odtwarzanie.
 */
void audio_stop();
