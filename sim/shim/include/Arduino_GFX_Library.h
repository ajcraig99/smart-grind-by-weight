// GFX Library for Arduino (Arduino_GFX) subset: the CO5300 panel writes into the twin's
// virtual framebuffer (sim/shim/src/display.cpp).
#pragma once

#include <cstdint>

#define RGB565_BLACK 0x0000
#define RGB565_WHITE 0xFFFF
#define GFX_NOT_DEFINED -1

class Arduino_DataBus {
public:
    virtual ~Arduino_DataBus() = default;
    virtual bool begin(int32_t = 0, int8_t = 0) { return true; }
};

class Arduino_ESP32QSPI : public Arduino_DataBus {
public:
    Arduino_ESP32QSPI(int8_t cs, int8_t sck, int8_t mosi, int8_t miso, int8_t quadwp, int8_t quadhd,
                      bool is_shared_interface = false)
        : cs_(cs) {
        (void)sck; (void)mosi; (void)miso; (void)quadwp; (void)quadhd; (void)is_shared_interface;
    }
private:
    int8_t cs_;
};

class Arduino_GFX {
public:
    Arduino_GFX(int16_t w, int16_t h) : width_(w), height_(h) {}
    virtual ~Arduino_GFX() = default;
    virtual bool begin(int32_t speed = GFX_NOT_DEFINED);
    int16_t width() const { return width_; }
    int16_t height() const { return height_; }
    virtual void fillScreen(uint16_t color);
    virtual void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t* bitmap, int16_t w, int16_t h);
    virtual void draw16bitRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h) {
        draw16bitRGBBitmap(x, y, const_cast<uint16_t*>(bitmap), w, h);
    }
    virtual void draw16bitBeRGBBitmap(int16_t x, int16_t y, uint16_t* bitmap, int16_t w, int16_t h);
    virtual void displayOn();
    virtual void displayOff();
protected:
    int16_t width_;
    int16_t height_;
};

class Arduino_CO5300 : public Arduino_GFX {
public:
    Arduino_CO5300(Arduino_DataBus* bus, int8_t rst = GFX_NOT_DEFINED, uint8_t r = 0, int16_t w = 280,
                   int16_t h = 456, uint8_t col_offset1 = 0, uint8_t row_offset1 = 0, uint8_t col_offset2 = 0,
                   uint8_t row_offset2 = 0)
        : Arduino_GFX(w, h), bus_(bus) {
        (void)rst; (void)r; (void)col_offset1; (void)row_offset1; (void)col_offset2; (void)row_offset2;
    }
    void setBrightness(uint8_t brightness);
private:
    Arduino_DataBus* bus_;
};
