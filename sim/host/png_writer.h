// Minimal PNG writer (RGB8, uncompressed deflate "stored" blocks) for framebuffer snapshots.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sim {

inline uint32_t png_crc(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

inline void png_put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

inline void png_chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    png_put32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    png_put32(out, png_crc(body.data(), body.size()) ^ 0xFFFFFFFFu);
}

// Write an RGB565 framebuffer as PNG. brightness scales the output (0..1).
inline bool write_png_rgb565(const std::string& path, const uint16_t* fb, int w, int h, float brightness = 1.0f) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (1 + 3 * w));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);  // filter: none
        for (int x = 0; x < w; ++x) {
            const uint16_t p = fb[y * w + x];
            const int r = ((p >> 11) & 0x1F) * 255 / 31, g = ((p >> 5) & 0x3F) * 255 / 63, b = (p & 0x1F) * 255 / 31;
            raw.push_back(static_cast<uint8_t>(r * brightness));
            raw.push_back(static_cast<uint8_t>(g * brightness));
            raw.push_back(static_cast<uint8_t>(b * brightness));
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t n = raw.size() - pos > 65535 ? 65535 : raw.size() - pos;
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + n));
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    png_put32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    png_put32(ihdr, static_cast<uint32_t>(w));
    png_put32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    png_chunk(out, "IHDR", ihdr);
    png_chunk(out, "IDAT", z);
    png_chunk(out, "IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace sim
