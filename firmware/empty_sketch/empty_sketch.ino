/*
 * Empty sketch z wyłączeniem BOD przez linker --wrap=esp_brownout_init
 * __wrap_esp_brownout_init zastępuje oryginalne esp_brownout_init w IDF startup
 */

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Zastępuje esp_brownout_init() z libesp_hw_support.a
// Wywoływane przez IDF startup ZAMIAST oryginalnej funkcji
extern "C" void __wrap_esp_brownout_init(void) {
    // Nic nie rób - nie inicjalizuj BOD
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);
    delay(2000);
    Serial.println("ESP32 OK - BOD disabled via linker wrap");
}

void loop() {
    delay(1000);
    Serial.println(".");
}
