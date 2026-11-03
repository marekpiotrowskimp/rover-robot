/*
 * ROVER Firmware - Autonomiczny Robot Asystent Domowy
 *
 * Hardware:
 *   - Seeed XIAO ESP32S3 Sense (ESP32-S3, 8MB PSRAM, 8MB Flash)
 *   - Kamera OV3660 (wbudowana w XIAO Sense)
 *   - Mikrofon PDM (wbudowany w XIAO Sense)
 *   - Audio BFF Add-on Adafruit 5769 (I2S, MAX98357A 3W)
 *   - LEGO Mindstorms NXT 1.0 (podwozie gąsienicowe, 2x silnik)
 *
 * Architektura:
 *   ESP32 = cienka warstwa sprzętowa (zmysły + ruch)
 *   Backend Python (FastAPI + Claude 3.5 Haiku) = inteligencja + pamięć
 *
 * Komunikacja:
 *   ESP32 <-> Backend: WiFi HTTP (audio WAV, odpowiedź WAV TTS, eventy)
 *   ESP32 <-> NXT:     Bluetooth Classic SPP (Direct Commands)
 *   Backend <-> UI:    HTTP + SSE
 *
 * Przepływ główny:
 *   [mikrofon PDM] -VAD-> [bufor WAV] -HTTP-> [backend: STT+Agent+TTS] -> [WAV] -> [I2S głośnik]
 *   [backend] -HTTP POST /cmd-> [ESP32 /cmd handler] -> [BT SPP] -> [NXT silniki]
 *
 * Biblioteki wymagane (Arduino Library Manager):
 *   - ArduinoJson         (Benoit Blanchon)
 *   - ESP8266Audio        (Earle F. Philhower III) - dla AudioGeneratorMP3
 *
 * Biblioteki wbudowane w ESP32 Arduino core:
 *   - esp_camera, esp_http_server, WiFi, HTTPClient, BluetoothSerial, driver/i2s
 *
 * Przed wgraniem:
 *   1. Edytuj config.h - ustaw WIFI_SSID, WIFI_PASS, BACKEND_HOST
 *   2. Board: "XIAO_ESP32S3" w Arduino IDE
 *   3. PSRAM: "OPI PSRAM" (wymagane dla bufora audio i kamery)
 *   4. Partition scheme: "Huge APP (3MB No OTA / 1MB SPIFFS)"
 *
 * Autor: ROVER Project, 2026
 */

#include <Arduino.h>
#include <WiFi.h>

// Moduły ROVER
#include "config.h"
#include "camera_module.h"
#include "web_server_esp.h"
#include "nxt_bluetooth.h"
#include "mic_capture.h"
#include "audio_player.h"
#include "wifi_transport.h"
#include "state_machine.h"

// =============================================================================
// Pomocnicze - oczekiwanie na WiFi z timeoutem
// =============================================================================
static bool wifi_connect() {
    WiFi.mode(WIFI_STA);
    // Ustaw statyczne IP żeby backend mógł zawsze snapować pod 192.168.0.189
    WiFi.config(
        IPAddress(192, 168, 0, 189),  // static IP
        IPAddress(192, 168, 0,   1),  // gateway
        IPAddress(255, 255, 255, 0),  // subnet
        IPAddress(192, 168, 0,   1)   // DNS
    );
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    DEBUG_PRINTF("[WIFI] Connecting to '%s'", WIFI_SSID);
    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > SM_WIFI_TIMEOUT_MS) {
            DEBUG_PRINTLN("\n[WIFI] Timeout!");
            return false;
        }
        delay(250);
        DEBUG_PRINT(".");
    }

    DEBUG_PRINTLN("");
    DEBUG_PRINTF("[WIFI] Connected! IP: %s  RSSI: %d dBm\n",
                 WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

// =============================================================================
// setup()
// =============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    // Poczekaj na Serial (USB CDC na ESP32-S3)
    unsigned long serial_wait = millis();
    while (!Serial && millis() - serial_wait < 3000) {}

    DEBUG_PRINTLN("\n========================================");
    DEBUG_PRINTLN("  ROVER Firmware v0.1");
    DEBUG_PRINTLN("  XIAO ESP32S3 Sense");
    DEBUG_PRINTLN("========================================\n");

    // ------------------------------------------------------------------
    // 1. Kamera OV3660
    // ------------------------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing camera...");
    if (!camera_init()) {
        DEBUG_PRINTLN("[BOOT] FATAL: Camera init failed");
        // Bez kamery nie ma sensu dalej - zapętl i czekaj na reset
        while (true) { delay(1000); }
    }

    // ------------------------------------------------------------------
    // 2. Mikrofon PDM
    // ------------------------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing microphone...");
    if (!mic_init()) {
        // Nie krytyczne - robot może działać bez mikrofonu (sterowanie z UI)
        DEBUG_PRINTLN("[BOOT] WARNING: Microphone init failed - voice disabled");
    }

    // ------------------------------------------------------------------
    // 3. I2S Audio (głośnik)
    // ------------------------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing audio output...");
    if (!audio_init()) {
        DEBUG_PRINTLN("[BOOT] WARNING: Audio init failed - speaker disabled");
    }

    // ------------------------------------------------------------------
    // 4. WiFi
    // ------------------------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Connecting to WiFi...");
    if (!wifi_connect()) {
        DEBUG_PRINTLN("[BOOT] FATAL: WiFi connection failed");
        while (true) { delay(1000); }
    }

    // ------------------------------------------------------------------
    // 5. HTTP Server (kamera + komendy)
    // ------------------------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Starting HTTP server...");
    web_server_start();

    // ------------------------------------------------------------------
    // 6. Bluetooth NXT (nieblokujące - próba połączenia w tle)
    // ------------------------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Connecting to NXT...");
    // nxt_init() może blokować do SM_BT_TIMEOUT_MS
    if (!nxt_init()) {
        DEBUG_PRINTLN("[BOOT] WARNING: NXT not connected - movement disabled");
        DEBUG_PRINTLN("[BOOT]         Check NXT is ON and discoverable");
    }

    // ------------------------------------------------------------------
    // 7. Maszyna stanów
    // ------------------------------------------------------------------
    sm_init();

    // ------------------------------------------------------------------
    // Gotowy!
    // ------------------------------------------------------------------
    DEBUG_PRINTLN("\n========================================");
    DEBUG_PRINTF("  ROVER Ready!\n");
    DEBUG_PRINTF("  Stream:  http://%s/\n",       WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("  Snap:    http://%s/snap\n",   WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("  Status:  http://%s/status\n", WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("  Command: http://%s/cmd\n",    WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("  NXT:     %s\n", nxt_is_connected() ? "connected" : "NOT connected");
    DEBUG_PRINTLN("========================================\n");

    // Wyślij event "boot" do backendu (asynchronicznie)
    transport_post_event("boot", nullptr);
}

// =============================================================================
// loop()
// =============================================================================
void loop() {
    // Główna maszyna stanów (VAD -> STT -> Agent -> TTS -> playback)
    sm_update();

    // Watchdog WiFi - próba reconnect przy utracie połączenia
    static unsigned long last_wifi_check = 0;
    if (millis() - last_wifi_check > 10000) {  // co 10 sekund
        last_wifi_check = millis();
        if (WiFi.status() != WL_CONNECTED) {
            DEBUG_PRINTLN("[WIFI] Lost connection, reconnecting...");
            WiFi.reconnect();
        }
    }

    // Mała pauza - oddaj czas dla zadań systemowych ESP32 (IDLE task, WDT)
    delay(5);
}
