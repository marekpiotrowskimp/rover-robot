#include "nxt_bluetooth.h"
#include "config.h"

// =============================================================================
// nxt_bluetooth.cpp - STUB
//
// ESP32-S3 z arduino-esp32 v3 wspiera tylko BLE (NimBLE), nie Classic BT.
// NXT LEGO wymaga Classic BT SPP - sterowanie przejął backend Python,
// który łączy się z NXT bezpośrednio przez BT Classic z komputera.
//
// ESP32 rejestruje tylko komendy w /cmd (do logów), nie wysyła ich do NXT.
// =============================================================================

bool nxt_init() {
    DEBUG_PRINTLN("[NXT] STUB: BT Classic not available on ESP32-S3 / arduino-esp32 v3.");
    DEBUG_PRINTLN("[NXT] Motor control is handled by backend Python via BT Classic.");
    return true;  // nie blokuj reszty firmware
}

bool nxt_is_connected() {
    return false;  // backend odpowiada za połączenie z NXT
}

void nxt_move(const char* direction, int speed, int duration_ms) {
    DEBUG_PRINTF("[NXT] STUB cmd: dir=%s spd=%d dur=%dms (handled by backend)\n",
                 direction, speed, duration_ms);
}

void nxt_move_dir(NxtDirection dir, int speed, int duration_ms) {
    DEBUG_PRINTF("[NXT] STUB move_dir: dir=%d spd=%d dur=%dms (handled by backend)\n",
                 (int)dir, speed, duration_ms);
}

void nxt_stop() {
    DEBUG_PRINTLN("[NXT] STUB stop (handled by backend)");
}

void nxt_set_motors(int power_left, int power_right, int duration_ms) {
    DEBUG_PRINTF("[NXT] STUB motors: L=%d R=%d dur=%dms (handled by backend)\n",
                 power_left, power_right, duration_ms);
}

void nxt_update() {
    // no-op
}
