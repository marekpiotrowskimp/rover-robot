#include "audio_player.h"
#include "config.h"

#include <Arduino.h>
#include <ESP_I2S.h>

// =============================================================================
// audio_player.cpp - Odtwarzanie WAV/MP3 przez I2S (nowe API arduino-esp32 v3)
//
// Zastąpiono ESP8266Audio (legacy I2S) przez wbudowany ESP_I2S który:
//   - używa nowego i2s_std driver (kompatybilny z kamerą)
//   - ma wbudowaną obsługę playWAV() i playMP3()
//
// Konfiguracja I2S (MAX98357A lub Audio BFF Add-on):
//   BCLK -> GPIO 8
//   LRCK -> GPIO 7
//   DOUT -> GPIO 43
// =============================================================================

static I2SClass _i2s_out;
static bool     _initialized = false;
static bool     _playing = false;

bool audio_init() {
    DEBUG_PRINTLN("[AUDIO] Initializing I2S output (new ESP_I2S API)...");

    // Konfiguracja pinów STD I2S (MAX98357A)
    _i2s_out.setPins(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN);

    // Inicjalizacja: STD mode, 16kHz, 16-bit, mono
    // (częstotliwość dopasowana do audio z backendu - można zmienić)
    if (!_i2s_out.begin(I2S_MODE_STD, 16000,
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        DEBUG_PRINTLN("[AUDIO] I2S STD begin failed");
        return false;
    }

    _initialized = true;
    DEBUG_PRINTF("[AUDIO] Ready - BCLK=%d LRCK=%d DOUT=%d\n",
                 I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN);
    return true;
}

void audio_play_wav(const uint8_t* data, size_t len) {
    if (!_initialized || !data || len == 0) return;

    DEBUG_PRINTF("[AUDIO] Playing WAV: %d bytes\n", (int)len);
    _playing = true;

    // ESP_I2S::playWAV parsuje nagłówek RIFF WAV i odtwarza PCM przez I2S
    _i2s_out.playWAV((uint8_t*)data, len);

    _playing = false;
    DEBUG_PRINTLN("[AUDIO] WAV playback done");
}

void audio_play_mp3(const uint8_t* data, size_t len) {
    if (!_initialized || !data || len == 0) return;

    DEBUG_PRINTF("[AUDIO] Playing MP3: %d bytes\n", (int)len);

#if ARDUINO_HAS_MP3_DECODER
    _playing = true;
    _i2s_out.playMP3((uint8_t*)data, len);
    _playing = false;
    DEBUG_PRINTLN("[AUDIO] MP3 playback done");
#else
    // Brak dekodera MP3 - backend powinien wysyłać WAV zamiast MP3
    DEBUG_PRINTLN("[AUDIO] MP3 decoder not available - use WAV format from backend");
#endif
}

bool audio_is_playing() {
    return _playing;
}

void audio_stop() {
    // ESP_I2S nie ma stop() dla playWAV - odtwarzanie jest synchroniczne (blokujące)
    // Flaga _playing jest ustawiana przed wywołaniem i czyszczona po powrocie
    _playing = false;
}
