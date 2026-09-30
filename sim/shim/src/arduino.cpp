// Arduino core functions on the sim clock and pin model.
#include <Arduino.h>

#include "../../core/scheduler.h"
#include "../../core/world.h"

HWCDC Serial;
EspClass ESP;

unsigned long millis() {
    sim::note_clock_read();
    return static_cast<unsigned long>(sim::now_us() / 1000ULL);
}

unsigned long micros() {
    sim::note_clock_read();
    return static_cast<unsigned long>(sim::now_us());
}

void delay(uint32_t ms) {
    if (!sim::in_task()) {
        sim::busy_advance_us(static_cast<uint64_t>(ms) * 1000ULL);
        return;
    }
    // Arduino-ESP32 delay() is vTaskDelay(ms / portTICK_PERIOD_MS).
    vTaskDelay(ms);
}

void delayMicroseconds(uint32_t us) { sim::busy_advance_us(us); }

void yield() { sim::yield(); }

void pinMode(uint8_t pin, uint8_t mode) { sim::pin_mode(pin, mode); }
void digitalWrite(uint8_t pin, uint8_t value) { sim::pin_write(pin, value); }
int digitalRead(uint8_t pin) { return sim::pin_read(pin); }

void noInterrupts() {}
void interrupts() {}

long random(long max_value) {
    if (max_value <= 0) return 0;
    return static_cast<long>(sim::firmware_random() % static_cast<uint32_t>(max_value));
}

long random(long min_value, long max_value) {
    if (min_value >= max_value) return min_value;
    return min_value + random(max_value - min_value);
}

void randomSeed(unsigned long) {}

// ---- USB CDC serial: output goes to the twin's log ----

void HWCDC::begin(unsigned long) {}
size_t HWCDC::write(uint8_t c) {
    const char ch = static_cast<char>(c);
    sim::log_write(&ch, 1);
    return 1;
}
size_t HWCDC::write(const uint8_t* buffer, size_t size) {
    sim::log_write(reinterpret_cast<const char*>(buffer), size);
    return size;
}
int HWCDC::available() { return 0; }
int HWCDC::read() { return -1; }
int HWCDC::peek() { return -1; }

// ---- ESP class: fixed plausible values for an ESP32-S3 N16R8 (placeholders) ----

uint32_t EspClass::getHeapSize() { return 320 * 1024; }
uint32_t EspClass::getFreeHeap() { return 180 * 1024; }
uint32_t EspClass::getMinFreeHeap() { return 150 * 1024; }
uint32_t EspClass::getMaxAllocHeap() { return 110 * 1024; }
uint32_t EspClass::getPsramSize() { return 8 * 1024 * 1024; }
uint32_t EspClass::getFreePsram() { return 7 * 1024 * 1024; }
uint32_t EspClass::getFlashChipSize() { return 16 * 1024 * 1024; }
uint32_t EspClass::getFlashChipSpeed() { return 80000000; }
uint32_t EspClass::getCpuFreqMHz() { return 240; }
uint64_t EspClass::getEfuseMac() { return 0x0000DEADBEEF0001ULL; }
const char* EspClass::getChipModel() { return "ESP32-S3 (digital twin)"; }
uint8_t EspClass::getChipRevision() { return 0; }
uint8_t EspClass::getChipCores() { return 2; }
const char* EspClass::getSdkVersion() { return "sim"; }
uint32_t EspClass::getSketchSize() { return 2 * 1024 * 1024; }
uint32_t EspClass::getFreeSketchSpace() { return 6 * 1024 * 1024; }
void EspClass::restart() { esp_restart(); }
