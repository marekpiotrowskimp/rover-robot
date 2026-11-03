#pragma once

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// nxt_bluetooth.h - STUB
//
// ESP32-S3 z arduino-esp32 v3 wspiera tylko BLE (NimBLE), nie Classic BT.
// NXT LEGO wymaga Classic BT SPP - sterowanie przejął backend Python,
// który łączy się z NXT bezpośrednio przez BT Classic z komputera.
//
// ESP32 rejestruje tylko komendy w /cmd (do logów), nie wysyła ich do NXT.
// =============================================================================

// Kierunki ruchu (zachowane dla kompatybilności z resztą kodu)
typedef enum {
    NXT_FORWARD  = 0,
    NXT_BACKWARD = 1,
    NXT_LEFT     = 2,
    NXT_RIGHT    = 3,
    NXT_STOP     = 4
} NxtDirection;

// Wszystkie funkcje są no-op stubs - logika BT jest na backendzie Python
bool nxt_init();
bool nxt_is_connected();
void nxt_move(const char* direction, int speed, int duration_ms);
void nxt_move_dir(NxtDirection dir, int speed, int duration_ms);
void nxt_stop();
void nxt_set_motors(int power_left, int power_right, int duration_ms);
void nxt_update();
