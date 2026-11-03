/*
 * Minimalny rover - tylko WiFi, bez kamery/I2S/mikrofonu
 * Test czy BOD crash pochodzi od WiFi init
 */
#include <Arduino.h>
#include <WiFi.h>

#include "secrets.h"   // WIFI_SSID, WIFI_PASS — patrz secrets.h.example
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}
    Serial.println("\nROVER minimal - WiFi test");

    WiFi.begin(ssid, password);
    Serial.print("Connecting...");
    while (WiFi.status() != WL_CONNECTED && millis() < 15000) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi timeout");
    }
    Serial.println("Setup done.");
}

void loop() {
    delay(5000);
    Serial.println("alive");
}
