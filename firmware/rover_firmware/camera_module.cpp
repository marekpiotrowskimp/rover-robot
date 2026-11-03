#include "camera_module.h"
#include "config.h"
#include "camera_pins.h"

// =============================================================================
// camera_module.cpp - Implementacja obsługi kamery OV3660
// Bazuje na kodzie inicjalizacji z EdgeNN_Streeming, zmieniony format na JPEG
// =============================================================================

static bool _camera_ready = false;

bool camera_init() {
    camera_config_t config;

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;

    // Piny danych (zdefiniowane w camera_pins.h przez CAMERA_MODEL_XIAO_ESP32S3)
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    // Piny sterujące
    config.pin_xclk    = XCLK_GPIO_NUM;
    config.pin_pclk    = PCLK_GPIO_NUM;
    config.pin_vsync   = VSYNC_GPIO_NUM;
    config.pin_href    = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn    = PWDN_GPIO_NUM;
    config.pin_reset   = RESET_GPIO_NUM;

    // Parametry kamery
    config.xclk_freq_hz  = CAM_XCLK_FREQ;
    config.frame_size    = CAM_FRAME_SIZE;       // FRAMESIZE_VGA = 640x480
    config.pixel_format  = PIXFORMAT_JPEG;       // JPEG - mniejsze CPU niż RGB565
    config.grab_mode     = CAMERA_GRAB_LATEST;       // zawsze najświeższa klatka (nie stary bufor)
    config.fb_location   = CAMERA_FB_IN_PSRAM;   // frame buffer w PSRAM (8MB)
    config.jpeg_quality  = CAM_JPEG_QUALITY;     // 12 = dobra jakość/rozmiar
    config.fb_count      = CAM_FB_COUNT;         // 2 bufory dla płynnego streamu

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        DEBUG_PRINTF("[CAMERA] Init failed: 0x%x\n", err);
        _camera_ready = false;
        return false;
    }

    // Opcjonalne poprawki jakości przez sensor
    sensor_t* s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_brightness(s, 0);      // -2 do 2
        s->set_contrast(s, 0);        // -2 do 2
        s->set_saturation(s, 0);      // -2 do 2
        s->set_sharpness(s, 0);       // -2 do 2
        s->set_whitebal(s, 1);        // auto white balance
        s->set_exposure_ctrl(s, 1);   // auto exposure
        s->set_gain_ctrl(s, 1);       // auto gain
        s->set_hmirror(s, 0);         // lustro poziome (0=off)
        s->set_vflip(s, 0);           // flip pionowy (0=off)
    }

    _camera_ready = true;
    DEBUG_PRINTF("[CAMERA] Ready - VGA 640x480 JPEG q%d\n", CAM_JPEG_QUALITY);
    return true;
}

camera_fb_t* camera_snapshot() {
    if (!_camera_ready) {
        DEBUG_PRINTLN("[CAMERA] Not ready - call camera_init() first");
        return NULL;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        DEBUG_PRINTLN("[CAMERA] Capture failed");
        return NULL;
    }

    // Format powinien być JPEG (skonfigurowany w camera_init)
    // Jeśli z jakiegoś powodu dostaniemy inny format, konwertujemy
    if (fb->format != PIXFORMAT_JPEG) {
        DEBUG_PRINTLN("[CAMERA] Warning: unexpected pixel format, frame may be invalid");
    }

    DEBUG_PRINTF("[CAMERA] Snapshot: %dx%d, %d bytes\n",
                 fb->width, fb->height, fb->len);
    return fb;
}

void camera_fb_return(camera_fb_t* fb) {
    if (fb != NULL) {
        esp_camera_fb_return(fb);
    }
}

bool camera_is_ready() {
    return _camera_ready;
}

const char* camera_resolution_str() {
    if (!_camera_ready) return "not initialized";
    // CAM_FRAME_SIZE = FRAMESIZE_VGA = 640x480
    return "640x480";
}
