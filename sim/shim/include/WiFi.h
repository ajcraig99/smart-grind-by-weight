// Declarations-only WiFi surface for headers; the radio is absent in the twin.
#pragma once
#include <Arduino.h>
typedef enum { WL_NO_SHIELD = 255, WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 } wl_status_t;

class IPAddress {
public:
    IPAddress() = default;
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : a_(a), b_(b), c_(c), d_(d) {}
    String toString() const {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a_, b_, c_, d_);
        return String(buf);
    }
private:
    uint8_t a_ = 0, b_ = 0, c_ = 0, d_ = 0;
};

class WiFiClass {
public:
    wl_status_t status() { return WL_DISCONNECTED; }
    IPAddress localIP() { return IPAddress(); }
    IPAddress softAPIP() { return IPAddress(192, 168, 4, 1); }
    String SSID() { return String(); }
};
extern WiFiClass WiFi;
