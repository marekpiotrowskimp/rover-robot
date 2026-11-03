#pragma once

// =============================================================================
// web_server_esp.h - HTTP Server ESP32
// Serwuje: MJPEG stream, JPEG snapshot, status JSON, komendy ruchu
// =============================================================================

/**
 * Uruchamia HTTP server na porcie HTTP_SERVER_PORT (config.h).
 * Rejestruje wszystkie endpointy.
 * Musi być wywołane po camera_init() i WiFi.begin().
 */
void web_server_start();

/**
 * Zatrzymuje HTTP server.
 */
void web_server_stop();

/**
 * Sprawdza czy server jest uruchomiony.
 */
bool web_server_is_running();

// Endpointy:
// GET /        -> MJPEG live stream z kamery (multipart/x-mixed-replace)
// GET /snap    -> pojedynczy JPEG snapshot (image/jpeg)
// GET /status  -> stan robota JSON {wifi_rssi, bt_connected, state, uptime_ms}
// POST /cmd    -> komenda ruchu JSON {direction, speed, duration_ms}
//                 direction: "forward"|"backward"|"left"|"right"|"stop"
//                 speed: 0-100
//                 duration_ms: 0=bez limitu
