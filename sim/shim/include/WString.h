// Arduino String for the twin (subset used by the firmware and the fake libraries).
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper*>(string_literal))

class String {
public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    String(const char* s, size_t n) : s_(s ? std::string(s, n) : std::string()) {}
    String(const __FlashStringHelper* s) : s_(s ? reinterpret_cast<const char*>(s) : "") {}
    String(const std::string& s) : s_(s) {}
    String(const String&) = default;
    String(String&&) = default;
    explicit String(char c) : s_(1, c) {}
    String(unsigned char v, unsigned char base = 10) { from_ulong(v, base); }
    String(int v, unsigned char base = 10) { from_long(v, base); }
    String(unsigned int v, unsigned char base = 10) { from_ulong(v, base); }
    String(long v, unsigned char base = 10) { from_long(v, base); }
    String(unsigned long v, unsigned char base = 10) { from_ulong(v, base); }
    String(long long v, unsigned char base = 10) { from_long(v, base); }
    String(unsigned long long v, unsigned char base = 10) { from_ulong(v, base); }
    String(float v, unsigned int decimals = 2) { from_double(v, decimals); }
    String(double v, unsigned int decimals = 2) { from_double(v, decimals); }

    String& operator=(const String&) = default;
    String& operator=(String&&) = default;
    String& operator=(const char* s) { s_ = s ? s : ""; return *this; }

    const char* c_str() const { return s_.c_str(); }
    unsigned int length() const { return static_cast<unsigned int>(s_.size()); }
    bool isEmpty() const { return s_.empty(); }
    bool reserve(unsigned int n) { s_.reserve(n); return true; }
    void clear() { s_.clear(); }

    char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
    void setCharAt(unsigned int i, char c) { if (i < s_.size()) s_[i] = c; }
    char operator[](unsigned int i) const { return charAt(i); }
    char& operator[](unsigned int i) { static char dummy; return i < s_.size() ? s_[i] : (dummy = 0); }

    bool concat(const String& o) { s_ += o.s_; return true; }
    bool concat(const char* o) { if (o) s_ += o; return true; }
    bool concat(const char* o, unsigned int n) { if (o) s_.append(o, n); return true; }
    bool concat(char c) { s_ += c; return true; }
    bool concat(unsigned char v) { return concat(String(v)); }
    bool concat(int v) { return concat(String(v)); }
    bool concat(unsigned int v) { return concat(String(v)); }
    bool concat(long v) { return concat(String(v)); }
    bool concat(unsigned long v) { return concat(String(v)); }
    bool concat(long long v) { return concat(String(v)); }
    bool concat(unsigned long long v) { return concat(String(v)); }
    bool concat(float v) { return concat(String(v)); }
    bool concat(double v) { return concat(String(v)); }

    template <typename T> String& operator+=(const T& v) { concat(v); return *this; }

    bool equals(const String& o) const { return s_ == o.s_; }
    bool equals(const char* o) const { return s_ == (o ? o : ""); }
    bool equalsIgnoreCase(const String& o) const {
        if (s_.size() != o.s_.size()) return false;
        for (size_t i = 0; i < s_.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(s_[i])) != std::tolower(static_cast<unsigned char>(o.s_[i]))) return false;
        }
        return true;
    }
    int compareTo(const String& o) const { return s_.compare(o.s_); }
    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator==(const char* o) const { return s_ == (o ? o : ""); }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    bool operator!=(const char* o) const { return !(*this == o); }
    bool operator<(const String& o) const { return s_ < o.s_; }
    explicit operator bool() const { return true; }

    bool startsWith(const String& p) const { return s_.compare(0, p.s_.size(), p.s_) == 0; }
    bool startsWith(const String& p, unsigned int offset) const {
        return offset <= s_.size() && s_.compare(offset, p.s_.size(), p.s_) == 0;
    }
    bool endsWith(const String& p) const {
        return p.s_.size() <= s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
    }

    int indexOf(char c, unsigned int from = 0) const { return to_index(s_.find(c, from)); }
    int indexOf(const String& p, unsigned int from = 0) const { return to_index(s_.find(p.s_, from)); }
    int lastIndexOf(char c) const { return to_index(s_.rfind(c)); }
    int lastIndexOf(const String& p) const { return to_index(s_.rfind(p.s_)); }

    String substring(unsigned int from) const { return from < s_.size() ? String(s_.substr(from)) : String(); }
    String substring(unsigned int from, unsigned int to) const {
        if (from > to) { unsigned int t = from; from = to; to = t; }
        if (from >= s_.size()) return String();
        if (to > s_.size()) to = static_cast<unsigned int>(s_.size());
        return String(s_.substr(from, to - from));
    }

    void replace(char a, char b) { for (auto& c : s_) if (c == a) c = b; }
    void replace(const String& a, const String& b) {
        if (a.s_.empty()) return;
        size_t pos = 0;
        while ((pos = s_.find(a.s_, pos)) != std::string::npos) {
            s_.replace(pos, a.s_.size(), b.s_);
            pos += b.s_.size();
        }
    }
    void remove(unsigned int index) { if (index < s_.size()) s_.erase(index); }
    void remove(unsigned int index, unsigned int count) { if (index < s_.size()) s_.erase(index, count); }
    void toLowerCase() { for (auto& c : s_) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    void toUpperCase() { for (auto& c : s_) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    void trim() {
        size_t b = 0, e = s_.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s_[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s_[e - 1]))) --e;
        s_ = s_.substr(b, e - b);
    }

    long toInt() const { return std::strtol(s_.c_str(), nullptr, 10); }
    float toFloat() const { return std::strtof(s_.c_str(), nullptr); }
    double toDouble() const { return std::strtod(s_.c_str(), nullptr); }

    void getBytes(unsigned char* buf, unsigned int len, unsigned int index = 0) const {
        if (!buf || len == 0) return;
        size_t n = 0;
        for (size_t i = index; i < s_.size() && n + 1 < len; ++i) buf[n++] = static_cast<unsigned char>(s_[i]);
        buf[n] = 0;
    }
    void toCharArray(char* buf, unsigned int len, unsigned int index = 0) const {
        getBytes(reinterpret_cast<unsigned char*>(buf), len, index);
    }

    const std::string& std_str() const { return s_; }

private:
    std::string s_;
    static int to_index(size_t pos) { return pos == std::string::npos ? -1 : static_cast<int>(pos); }
    void from_long(long long v, unsigned char base) {
        if (base == 10) { s_ = std::to_string(v); return; }
        if (v < 0) { from_ulong(static_cast<unsigned long long>(-v), base); s_ = "-" + s_; return; }
        from_ulong(static_cast<unsigned long long>(v), base);
    }
    void from_ulong(unsigned long long v, unsigned char base) {
        if (base < 2 || base > 36) base = 10;
        char buf[72];
        int i = 70;
        buf[71] = 0;
        if (v == 0) buf[i--] = '0';
        while (v > 0) {
            const int d = static_cast<int>(v % base);
            buf[i--] = static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10);
            v /= base;
        }
        s_ = &buf[i + 1];
    }
    void from_double(double v, unsigned int decimals) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(decimals), v);
        s_ = buf;
    }
};

inline String operator+(const String& a, const String& b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, const char* b) { String r(a); r.concat(b); return r; }
inline String operator+(const char* a, const String& b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, char b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, int b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, unsigned int b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, long b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, unsigned long b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, float b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, double b) { String r(a); r.concat(b); return r; }
inline bool operator==(const char* a, const String& b) { return b == a; }
