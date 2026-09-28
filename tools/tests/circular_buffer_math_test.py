"""Compile the real sample buffer against a controllable clock."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUFFER = ROOT / "src/hardware/circular_buffer_math"

ARDUINO = r'''
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <alloca.h>
inline std::atomic<uint32_t> fake_now_ms{0};
inline unsigned long millis() { return fake_now_ms.load(); }
using std::abs;
'''
CONSTANTS = r'''
#pragma once
#define HW_LOADCELL_SAMPLE_RATE_SPS 10
#define HW_LOADCELL_SAMPLE_INTERVAL_MS (1000 / HW_LOADCELL_SAMPLE_RATE_SPS)
#define SYS_DISPLAY_FILTER_ALPHA_DOWN 0.3f
#define LOG_LOADCELL_DEBUG(...) ((void)0)
'''
CASES = r'''
#include "hardware/circular_buffer_math/circular_buffer_math.h"
#include <cassert>
#include <thread>
#include <vector>

// Feed `count` samples 100 ms apart ending at `end_ms`, then set the clock.
static void feed(CircularBufferMath& buffer, int count, uint32_t end_ms, int32_t start, int32_t step) {
    for (int i = 0; i < count; ++i) {
        const uint32_t t = end_ms - static_cast<uint32_t>((count - 1 - i) * 100);
        fake_now_ms = t;
        buffer.add_sample(start + i * step, t);
    }
}

// Number of samples no older than window_ms (public API only).
static int samples_in(const CircularBufferMath& buffer, uint32_t window_ms) {
    int32_t delta = 0; uint32_t span = 0; int used = 0;
    buffer.get_window_delta(window_ms, &delta, &span, &used);
    return used;
}

int main() {
    // Windows use sample ages, so they stay correct across the millis() wrap.
    {
        CircularBufferMath buffer;
        const uint32_t end = 250;                  // 10 samples straddle 0xFFFFFFFF -> 0
        feed(buffer, 10, end, 1000, 0);
        fake_now_ms = end + 10;
        assert(buffer.get_smoothed_raw(300) == 1000);
        int32_t delta = -1; uint32_t span = 0; int used = 0;
        assert(buffer.get_window_delta(1000, &delta, &span, &used));
        assert(delta == 0 && used == 10 && span == 900);
        assert(buffer.get_buffer_time_span_ms() == 900);
        // Samples from before the wrap are old, not "in the future".
        fake_now_ms = end + 5000;
        assert(buffer.get_min_raw(1000) == 0 && buffer.get_max_raw(1000) == 0);
        assert(!buffer.is_settled(500, 1000));
    }

    // Flow rate across the wrap: +100 raw per 100 ms = 1000 raw/s.
    {
        CircularBufferMath buffer;
        feed(buffer, 30, 1200, 0, 100);
        fake_now_ms = 1200;
        const float flow = buffer.get_raw_flow_rate(1000);
        assert(flow > 999.0f && flow < 1001.0f);
        const float p95 = buffer.get_raw_flow_rate_95th_percentile(2500);
        assert(p95 > 999.0f && p95 < 1001.0f);
    }

    // Settling needs at least three samples; one or two samples never settle.
    {
        CircularBufferMath buffer;
        fake_now_ms = 5000;
        assert(!buffer.is_settled(200, 50));
        buffer.add_sample(1000, 5000);
        assert(!buffer.is_settled(200, 50));
        fake_now_ms = 5100; buffer.add_sample(1000, 5100);
        assert(!buffer.is_settled(200, 50));
        fake_now_ms = 5200; buffer.add_sample(1000, 5200);
        fake_now_ms = 5210;
        assert(buffer.is_settled(200, 50));        // widened to 350 ms: 3 samples
    }

    // A steady trickle with a small spread is not settled; noise within limits is.
    {
        CircularBufferMath trickle;
        feed(trickle, 6, 10000, 0, 30);            // 30 raw per sample, spread ~45
        fake_now_ms = 10000;
        assert(samples_in(trickle, 500) == 6);
        assert(!trickle.is_settled(500, 50));      // drift 150 > 2 x 50
        CircularBufferMath noisy;
        const int32_t values[] = {0, 40, -30, 20, -40, 10};
        for (int i = 0; i < 6; ++i) { fake_now_ms = 20000 + i * 100; noisy.add_sample(values[i], 20000 + i * 100); }
        assert(noisy.is_settled(500, 50));
    }

    // A clear requested by another task reads as empty at once and is applied
    // by the next sample, which starts a fresh window.
    {
        CircularBufferMath buffer;
        feed(buffer, 20, 3000, 500, 0);
        fake_now_ms = 3000;
        std::thread([&] { buffer.clear_all_samples(); }).join();
        assert(buffer.get_sample_count() == 0);
        assert(samples_in(buffer, 10000) == 0 && buffer.get_max_raw(10000) == 0);
        int32_t raw = -1; uint32_t stamp = 0;
        assert(!buffer.get_latest_sample(&raw, &stamp));
        fake_now_ms = 3100; buffer.add_sample(7, 3100);
        assert(buffer.get_sample_count() == 1);
        assert(buffer.get_latest_sample(&raw, &stamp) && raw == 7 && stamp == 3100);
        assert(buffer.get_min_raw(10000) == 7 && buffer.get_max_raw(10000) == 7);
        assert(buffer.get_buffer_time_span_ms() == 0);
    }

    // Concurrent producer, readers and clears: every reader sees a coherent,
    // newest-first window of values the producer wrote (value == timestamp).
    {
        CircularBufferMath buffer;
        std::atomic<bool> done{false};
        std::thread producer([&] {
            for (uint32_t t = 1; t <= 20000; ++t) {
                fake_now_ms = t;
                buffer.add_sample(static_cast<int32_t>(t), t);
            }
            done = true;
        });
        std::vector<std::thread> readers;
        for (int r = 0; r < 3; ++r) {
            readers.emplace_back([&, r] {
                while (!done) {
                    // One snapshot per call: the value change equals the time span.
                    int32_t delta = 0; uint32_t span = 0; int used = 0;
                    if (buffer.get_window_delta(40, &delta, &span, &used)) {
                        assert(delta == static_cast<int32_t>(span) && used >= 2 && used <= 41);
                    }
                    int32_t raw = 0; uint32_t stamp = 0;
                    if (buffer.get_latest_sample(&raw, &stamp)) assert(raw == static_cast<int32_t>(stamp));
                    (void)buffer.get_display_raw();
                    (void)buffer.is_settled(500, 10);
                    if (r == 0 && (stamp % 997) == 0) buffer.clear_all_samples();
                }
            });
        }
        producer.join();
        for (auto& reader : readers) reader.join();
    }
    return 0;
}
'''


class CircularBufferMathTest(unittest.TestCase):
    def build_and_run(self, sanitizer):
        with tempfile.TemporaryDirectory(prefix="smart-grind-buffer-test-") as folder:
            root = Path(folder)
            target = root / "src/hardware/circular_buffer_math"
            target.mkdir(parents=True)
            for name in ("circular_buffer_math.h", "circular_buffer_math.cpp"):
                shutil.copy(BUFFER / name, target / name)
            (root / "src/config").mkdir(parents=True)
            (root / "src/config/constants.h").write_text(CONSTANTS)
            (root / "include").mkdir()
            (root / "include/Arduino.h").write_text(ARDUINO)
            (root / "cases.cpp").write_text(CASES)
            binary = root / "test"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-O1", "-pthread",
                            f"-fsanitize={sanitizer}", "-I", str(root / "include"),
                            "-I", str(root / "src"), str(root / "cases.cpp"),
                            str(target / "circular_buffer_math.cpp"), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True, timeout=120)

    def test_windows_settling_and_clear(self):
        self.build_and_run("address,undefined")

    def test_cross_task_access_has_no_data_race(self):
        self.build_and_run("thread")


if __name__ == "__main__":
    unittest.main()
