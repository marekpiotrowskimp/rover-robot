#include "mic_capture.h"
#include "config.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Forward declaration
static void _finalize_recording();

// =============================================================================
// mic_capture.cpp - Mikrofon PDM + VAD + bufor WAV
//
// XIAO ESP32S3 Sense - wbudowany mikrofon PDM:
//   PDM_CLK  = GPIO 42  (WS  w I2S.setAllPins)
//   PDM_DATA = GPIO 41  (DIN w I2S.setAllPins)
//
// Wzorowane na: xiao_esp32s3_microphone.ino (Seeed/EdgeImpulse)
//   I2S.setAllPins(-1, 42, 41, -1, -1)
//   I2S.begin(PDM_MONO_MODE, 16000, 16)
//   esp_i2s::i2s_read(esp_i2s::I2S_NUM_0, ...)
//
// Próbkowanie 16kHz mono 16-bit - format wymagany przez Whisper STT.
// Wzmocnienie x8 (jak w przykładzie Seeed).
// =============================================================================

// Rozmiar bufora DMA odczytu (próbki * 2 bajty)
#define MIC_DMA_READ_BYTES  (512 * sizeof(int16_t))

// Bufor tymczasowy DMA
static int16_t _dma_buf[MIC_DMA_READ_BYTES / sizeof(int16_t)];

// Bufor nagrania w PSRAM
static int16_t* _rec_buf = NULL;
static size_t   _rec_samples = 0;
static size_t   _rec_max_samples = 0;

// Bufor WAV w PSRAM
static uint8_t* _wav_buf = NULL;
static size_t   _wav_len = 0;

// Stan modułu
static MicState  _state = MIC_IDLE;
static uint16_t  _current_rms = 0;

// VAD tracking
static unsigned long _speech_start_ms  = 0;
static unsigned long _silence_start_ms = 0;

// I2S instance (nowe API - PDM RX)
static I2SClass _i2s_mic;

// -----------------------------------------------------------------------------
// WAV nagłówek RIFF
// -----------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    char     riff_id[4];
    uint32_t riff_size;
    char     wave_id[4];
    char     fmt_id[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     data_id[4];
    uint32_t data_size;
} WavHeader;

static void _write_wav_header(uint8_t* buf, uint32_t data_bytes) {
    WavHeader* h = (WavHeader*)buf;
    memcpy(h->riff_id,   "RIFF", 4);
    h->riff_size       = data_bytes + sizeof(WavHeader) - 8;
    memcpy(h->wave_id,   "WAVE", 4);
    memcpy(h->fmt_id,    "fmt ", 4);
    h->fmt_size        = 16;
    h->audio_format    = 1;
    h->num_channels    = MIC_CHANNELS;
    h->sample_rate     = MIC_SAMPLE_RATE;
    h->byte_rate       = MIC_SAMPLE_RATE * MIC_CHANNELS * (MIC_SAMPLE_BITS / 8);
    h->block_align     = MIC_CHANNELS * (MIC_SAMPLE_BITS / 8);
    h->bits_per_sample = MIC_SAMPLE_BITS;
    memcpy(h->data_id,   "data", 4);
    h->data_size       = data_bytes;
}

static uint16_t _calc_rms(const int16_t* samples, size_t count) {
    if (count == 0) return 0;
    int64_t sum_sq = 0;
    for (size_t i = 0; i < count; i++) {
        sum_sq += (int64_t)samples[i] * samples[i];
    }
    return (uint16_t)sqrt((double)(sum_sq / count));
}

// -----------------------------------------------------------------------------
// Inicjalizacja - piny z xiao_esp32s3_microphone.ino: CLK=42, DATA=41
// -----------------------------------------------------------------------------
bool mic_init() {
    DEBUG_PRINTLN("[MIC] Initializing PDM microphone (ESP_I2S PDM RX)...");

    // Piny PDM - identycznie jak w xiao_esp32s3_microphone.ino:
    //   I2S.setAllPins(-1, 42, 41, -1, -1)  ->  CLK=42, DATA=41
    _i2s_mic.setPinsPdmRx(PDM_CLK_PIN, PDM_DATA_PIN);

    if (!_i2s_mic.begin(I2S_MODE_PDM_RX, MIC_SAMPLE_RATE,
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        DEBUG_PRINTLN("[MIC] I2S PDM RX begin failed");
        return false;
    }

    // Alokuj bufor nagrania w PSRAM
    _rec_max_samples = (MIC_SAMPLE_RATE * VAD_MAX_RECORD_MS) / 1000;
    _rec_buf = (int16_t*)ps_malloc(_rec_max_samples * sizeof(int16_t));
    if (!_rec_buf) {
        DEBUG_PRINTLN("[MIC] PSRAM alloc failed for rec_buf");
        _i2s_mic.end();
        return false;
    }

    // Alokuj bufor WAV
    size_t wav_max_bytes = sizeof(WavHeader) + _rec_max_samples * sizeof(int16_t);
    _wav_buf = (uint8_t*)ps_malloc(wav_max_bytes);
    if (!_wav_buf) {
        DEBUG_PRINTLN("[MIC] PSRAM alloc failed for wav_buf");
        free(_rec_buf);
        _i2s_mic.end();
        return false;
    }

    _state = MIC_IDLE;
    DEBUG_PRINTF("[MIC] Ready - %dHz, %d-bit mono, max %dms\n",
                 MIC_SAMPLE_RATE, MIC_SAMPLE_BITS, VAD_MAX_RECORD_MS);
    return true;
}

// -----------------------------------------------------------------------------
// Update - wywoływać w loop()
// -----------------------------------------------------------------------------
void mic_update() {
    if (_state == MIC_READY) return;

    // Odczyt przez ESP_I2S::readBytes
    size_t bytes_read = _i2s_mic.readBytes((char*)_dma_buf, MIC_DMA_READ_BYTES);
    if (bytes_read == 0) return;

    size_t samples_read = bytes_read / sizeof(int16_t);

    // Wzmocnienie x8 (mikrofon XIAO jest cichy - jak w przykładzie Seeed)
    for (size_t i = 0; i < samples_read; i++) {
        _dma_buf[i] = (int16_t)(_dma_buf[i] * 8);
    }

    _current_rms = _calc_rms(_dma_buf, samples_read);
    bool voice_active = (_current_rms > VAD_THRESHOLD);
    unsigned long now = millis();

    if (_state == MIC_IDLE) {
        if (voice_active) {
            _state = MIC_DETECTING;
            _rec_samples = 0;
            _speech_start_ms = now;
            _silence_start_ms = 0;
            DEBUG_PRINTF("[MIC] Voice detected (RMS=%d), recording...\n", _current_rms);
        }
    }

    if (_state == MIC_DETECTING) {
        size_t space_left = _rec_max_samples - _rec_samples;
        size_t to_copy = (samples_read < space_left) ? samples_read : space_left;
        memcpy(_rec_buf + _rec_samples, _dma_buf, to_copy * sizeof(int16_t));
        _rec_samples += to_copy;

        if (voice_active) {
            _silence_start_ms = 0;
        } else {
            if (_silence_start_ms == 0) {
                _silence_start_ms = now;
            }
            unsigned long silence_duration = now - _silence_start_ms;
            if (silence_duration >= VAD_SILENCE_MS) {
                unsigned long speech_duration = now - _speech_start_ms;
                if (speech_duration >= VAD_MIN_SPEECH_MS) {
                    _finalize_recording();
                } else {
                    DEBUG_PRINTF("[MIC] Too short (%lums), ignoring\n", speech_duration);
                    _state = MIC_IDLE;
                    _rec_samples = 0;
                }
                return;
            }
        }

        if (_rec_samples >= _rec_max_samples) {
            DEBUG_PRINTLN("[MIC] Max recording time reached");
            _finalize_recording();
        }
    }
}

static void _finalize_recording() {
    uint32_t data_bytes = _rec_samples * sizeof(int16_t);
    _wav_len = sizeof(WavHeader) + data_bytes;
    _write_wav_header(_wav_buf, data_bytes);
    memcpy(_wav_buf + sizeof(WavHeader), _rec_buf, data_bytes);
    _state = MIC_READY;
    DEBUG_PRINTF("[MIC] Recording ready: %d samples = %.1fs, %d bytes WAV\n",
                 (int)_rec_samples,
                 (float)_rec_samples / MIC_SAMPLE_RATE,
                 (int)_wav_len);
}

// -----------------------------------------------------------------------------
// API publiczne
// -----------------------------------------------------------------------------
MicState mic_get_state()                          { return _state; }
bool     mic_is_ready()                           { return _state == MIC_READY; }
uint16_t mic_get_rms()                            { return _current_rms; }

const uint8_t* mic_get_wav(size_t* out_len) {
    if (_state != MIC_READY || !_wav_buf) { *out_len = 0; return NULL; }
    *out_len = _wav_len;
    return _wav_buf;
}

void mic_drain() {
    // Opróżnij bufor DMA I2S — odczytaj i odrzuć nazbierane próbki.
    // Wywołuj podczas SM_SENDING / SM_PLAYING żeby DMA się nie przepełniał.
    // Używamy readBytes z krótkim timeoutem zamiast available() (niestabilne dla PDM).
    // Maksymalnie 32 porcje × 512 próbek = ~1 sekunda audio do odrzucenia.
    const int MAX_DRAIN_CHUNKS = 32;
    size_t drained = 0;
    for (int i = 0; i < MAX_DRAIN_CHUNKS; i++) {
        size_t n = _i2s_mic.readBytes((char*)_dma_buf, MIC_DMA_READ_BYTES);
        if (n == 0) break;
        drained += n;
    }
    if (drained > 0) {
        DEBUG_PRINTF("[MIC] Drained %d bytes from DMA\n", (int)drained);
    }
}

void mic_reset() {
    _state = MIC_IDLE;
    _rec_samples = 0;
    _wav_len = 0;
    _silence_start_ms = 0;
    _speech_start_ms = 0;
    DEBUG_PRINTLN("[MIC] Reset - listening...");
}
