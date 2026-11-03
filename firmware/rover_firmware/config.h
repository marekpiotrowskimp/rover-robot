#pragma once

#include <Arduino.h>

// =============================================================================
// ROVER Firmware - Konfiguracja
// Zmień poniższe wartości przed wgraniem firmware na ESP32
// =============================================================================

// -----------------------------------------------------------------------------
// WiFi
// -----------------------------------------------------------------------------
#include "secrets.h"   // WIFI_SSID, WIFI_PASS — patrz secrets.h.example

// -----------------------------------------------------------------------------
// Statyczne IP ESP32 (żeby backend mógł snapować kamerę pod stałym adresem)
// -----------------------------------------------------------------------------
#define ESP32_STATIC_IP  "192.168.0.189"
#define ESP32_GATEWAY    "192.168.0.1"
#define ESP32_SUBNET     "255.255.255.0"
#define ESP32_DNS        "192.168.0.1"

// -----------------------------------------------------------------------------
// Backend Python (FastAPI) - IP komputera w sieci lokalnej
// -----------------------------------------------------------------------------
#define BACKEND_HOST     "192.168.0.198"
#define BACKEND_PORT     8000
#define BACKEND_BASE_URL "http://" BACKEND_HOST ":" STR(BACKEND_PORT)

// Makro do stringify int
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// Endpointy backendu
#define BACKEND_SPEAK_URL  BACKEND_BASE_URL "/api/speak"   // POST: WAV -> MP3
#define BACKEND_EVENT_URL  BACKEND_BASE_URL "/api/event"   // POST: zdarzenia ESP32
#define BACKEND_CMD_URL    BACKEND_BASE_URL "/api/cmd"     // GET: komendy ruchów (polling)

// -----------------------------------------------------------------------------
// HTTP Server ESP32
// -----------------------------------------------------------------------------
#define HTTP_SERVER_PORT  80

// -----------------------------------------------------------------------------
// Kamera OV3660 (XIAO ESP32S3 Sense)
// Piny zdefiniowane w camera_pins.h przez CAMERA_MODEL_XIAO_ESP32S3
// -----------------------------------------------------------------------------
#define CAMERA_MODEL_XIAO_ESP32S3

// Ustawienia obrazu
#define CAM_FRAME_SIZE    FRAMESIZE_QVGA   // 320x240 (szybsze przesyłanie do LLM)
#define CAM_JPEG_QUALITY  12               // 0-63, niżej = lepsza jakość
#define CAM_FB_COUNT      2                // liczba frame bufferów w PSRAM
#define CAM_XCLK_FREQ     20000000         // 20 MHz

// -----------------------------------------------------------------------------
// I2S - Audio BFF Add-on (Adafruit 5769 montowany back-to-back na XIAO)
// MAX98357A klasy D, 3W
// -----------------------------------------------------------------------------
#define I2S_BCLK_PIN      8
#define I2S_LRCK_PIN      7
#define I2S_DOUT_PIN      43
#define I2S_SAMPLE_RATE   44100

// -----------------------------------------------------------------------------
// PDM Mikrofon (wbudowany w XIAO ESP32S3 Sense)
// -----------------------------------------------------------------------------
#define PDM_CLK_PIN       42
#define PDM_DATA_PIN      41
#define MIC_SAMPLE_RATE   16000   // Hz - wymagane przez Whisper STT
#define MIC_SAMPLE_BITS   16
#define MIC_CHANNELS      1       // mono

// VAD (Voice Activity Detection)
#define VAD_THRESHOLD     500     // próg RMS powyżej którego = aktywna mowa
#define VAD_SILENCE_MS    800     // ms ciszy = koniec wypowiedzi (było 1500 — za długie)
#define VAD_MAX_RECORD_MS 8000    // max czas nagrania w ms (było 5000 — za krótkie)
#define VAD_MIN_SPEECH_MS 300     // minimalna długość mowy (filtruje szumy)

// Rozmiar bufora audio: 8 sek * 16000 Hz * 2 bajty = 256 000 bajtów
#define AUDIO_BUF_SIZE    (MIC_SAMPLE_RATE * (VAD_MAX_RECORD_MS / 1000) * (MIC_SAMPLE_BITS / 8))

// -----------------------------------------------------------------------------
// Bluetooth NXT
// -----------------------------------------------------------------------------
#define NXT_DEVICE_NAME   "NXT"      // nazwa cegły NXT widoczna przez BT
#define NXT_PIN           "1234"     // domyślny PIN NXT
// Opcjonalnie: podaj MAC adres cegły (bardziej niezawodne niż nazwa)
// Format: {0x00, 0x16, 0x53, 0xXX, 0xXX, 0xXX}
// #define NXT_USE_MAC
// #define NXT_MAC_ADDR  {0x00, 0x16, 0x53, 0x00, 0x00, 0x00}

// Parametry ruchu
#define NXT_MOTOR_LEFT    0x01     // port B - lewy silnik
#define NXT_MOTOR_RIGHT   0x02     // port C - prawy silnik
#define NXT_DEFAULT_SPEED 75       // prędkość domyślna 0-100
#define NXT_TURN_SPEED    60       // prędkość przy skrętach

// Czas skrętu o ~90 stopni w ms (skalibruj eksperymentalnie)
#define NXT_TURN_90_MS    900

// -----------------------------------------------------------------------------
// State Machine
// -----------------------------------------------------------------------------
#define SM_WIFI_TIMEOUT_MS    20000   // timeout połączenia WiFi
#define SM_BT_TIMEOUT_MS      10000   // timeout połączenia BT z NXT
#define SM_HTTP_TIMEOUT_MS    30000   // timeout żądań HTTP (STT+LLM+TTS może trwać >5s)

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------
#define SERIAL_BAUD       115200
#define DEBUG_ENABLED     true

#if DEBUG_ENABLED
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif
