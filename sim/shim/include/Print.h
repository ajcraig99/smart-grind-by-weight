// Arduino Print/Stream base classes (subset).
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "WString.h"

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        while (size--) n += write(*buffer++);
        return n;
    }
    size_t write(const char* s) { return s ? write(reinterpret_cast<const uint8_t*>(s), std::strlen(s)) : 0; }
    size_t write(const char* s, size_t n) { return write(reinterpret_cast<const uint8_t*>(s), n); }
    virtual void flush() {}

    size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3))) {
        char buf[1024];
        va_list args;
        va_start(args, format);
        int n = std::vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        if (n <= 0) return 0;
        size_t len = static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n) : sizeof(buf) - 1;
        return write(reinterpret_cast<const uint8_t*>(buf), len);
    }
    size_t print(const String& s) { return write(s.c_str()); }
    size_t print(const char* s) { return write(s); }
    size_t print(const __FlashStringHelper* s) { return write(reinterpret_cast<const char*>(s)); }
    size_t print(char c) { return write(static_cast<uint8_t>(c)); }
    size_t print(int v, int base = DEC) { return print(String(v, static_cast<unsigned char>(base))); }
    size_t print(unsigned int v, int base = DEC) { return print(String(v, static_cast<unsigned char>(base))); }
    size_t print(long v, int base = DEC) { return print(String(v, static_cast<unsigned char>(base))); }
    size_t print(unsigned long v, int base = DEC) { return print(String(v, static_cast<unsigned char>(base))); }
    size_t print(long long v, int base = DEC) { return print(String(v, static_cast<unsigned char>(base))); }
    size_t print(unsigned long long v, int base = DEC) { return print(String(v, static_cast<unsigned char>(base))); }
    size_t print(double v, int digits = 2) { return print(String(v, static_cast<unsigned int>(digits))); }
    size_t println() { return write("\r\n"); }
    template <typename T> size_t println(const T& v) { size_t n = print(v); return n + println(); }
    template <typename T> size_t println(const T& v, int f) { size_t n = print(v, f); return n + println(); }
};

class Stream : public Print {
public:
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }
    size_t readBytes(uint8_t* buffer, size_t length) {
        size_t n = 0;
        while (n < length) { int c = read(); if (c < 0) break; buffer[n++] = static_cast<uint8_t>(c); }
        return n;
    }
    size_t readBytes(char* buffer, size_t length) { return readBytes(reinterpret_cast<uint8_t*>(buffer), length); }
    String readString() { String s; int c; while ((c = read()) >= 0) s += static_cast<char>(c); return s; }
    String readStringUntil(char t) { String s; int c; while ((c = read()) >= 0 && c != t) s += static_cast<char>(c); return s; }
    void setTimeout(unsigned long) {}
};
