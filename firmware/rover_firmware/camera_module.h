#pragma once

#include "esp_camera.h"
#include <stdint.h>
#include <stddef.h>

// =============================================================================
// camera_module.h - Obsługa kamery OV3660
// Inicjalizacja, MJPEG streaming, JPEG snapshot
// =============================================================================

/**
 * Inicjalizuje kamerę OV3660 w trybie JPEG.
 * Musi być wywołane przed pierwszym użyciem kamery.
 * @return true jeśli inicjalizacja powiodła się
 */
bool camera_init();

/**
 * Pobiera pojedynczy snapshot jako JPEG.
 * Zwrócony bufor jest własnością frame buffer systemu kamery.
 * Należy zwolnić przez camera_fb_return() po użyciu.
 * @return wskaźnik na camera_fb_t lub NULL przy błędzie
 */
camera_fb_t* camera_snapshot();

/**
 * Zwraca frame buffer do systemu kamery (musi być wywołane po snapshot).
 */
void camera_fb_return(camera_fb_t* fb);

/**
 * Sprawdza czy kamera jest zainicjalizowana.
 */
bool camera_is_ready();

/**
 * Zwraca aktualną rozdzielczość jako string (np. "640x480").
 */
const char* camera_resolution_str();
