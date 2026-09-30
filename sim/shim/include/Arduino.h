// Arduino-ESP32 core API for the digital twin. Time comes from the sim clock.
#pragma once

#include <algorithm>
#include <climits>
#include <limits.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "Print.h"
#include "WString.h"

using std::max;
using std::min;
using std::isinf;
using std::isnan;

typedef bool boolean;
typedef uint8_t byte;
typedef uint16_t word;

#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x01
#define OUTPUT 0x03
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define INPUT_PULLDOWN 0x09
#define OPEN_DRAIN 0x10
#define OUTPUT_OPEN_DRAIN 0x13

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#ifndef DRAM_ATTR
#define DRAM_ATTR
#endif
#ifndef PROGMEM
#define PROGMEM
#endif
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t*>(addr))

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)

unsigned long millis();
unsigned long micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield();

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);

void noInterrupts();
void interrupts();

long random(long max_value);
long random(long min_value, long max_value);
void randomSeed(unsigned long seed);

// Arduino-ESP32 log macros.
#define log_e(format, ...) ((void)0)
#define log_w(format, ...) ((void)0)
#define log_i(format, ...) ((void)0)
#define log_d(format, ...) ((void)0)
#define log_v(format, ...) ((void)0)

class HWCDC : public Stream {
public:
    void begin(unsigned long baud = 115200);
    void end() {}
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t size) override;
    using Print::write;
    int available() override;
    int read() override;
    int peek() override;
    void flush() override {}
    void setTxTimeoutMs(uint32_t) {}
    void setRxBufferSize(size_t) {}
    explicit operator bool() const { return true; }
};
extern HWCDC Serial;

class EspClass {
public:
    uint32_t getHeapSize();
    uint32_t getFreeHeap();
    uint32_t getMinFreeHeap();
    uint32_t getMaxAllocHeap();
    uint32_t getPsramSize();
    uint32_t getFreePsram();
    uint32_t getFlashChipSize();
    uint32_t getFlashChipSpeed();
    uint32_t getCpuFreqMHz();
    uint64_t getEfuseMac();
    const char* getChipModel();
    uint8_t getChipRevision();
    uint8_t getChipCores();
    const char* getSdkVersion();
    uint32_t getSketchSize();
    uint32_t getFreeSketchSpace();
    void restart();
};
extern EspClass ESP;
