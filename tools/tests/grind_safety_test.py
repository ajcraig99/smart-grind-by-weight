"""Run production grind-safety code: dry-run detection and the removal guard."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def compile_and_run(test, code, *sources):
    with tempfile.TemporaryDirectory(prefix="smart-grind-safety-test-") as folder:
        cpp = Path(folder) / "test.cpp"
        binary = Path(folder) / "test"
        cpp.write_text(code)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                        "-I", str(ROOT / "src"), "-I", str(ROOT), str(cpp), *sources,
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=20)


class GrindSafetyTest(unittest.TestCase):
    def test_removal_guard_rules(self):
        # The simulator suite only builds on Windows; run its guard test here too.
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "guard"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(ROOT / "src"),
                            str(ROOT / "sim/net_weight_guard_test.cpp"), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)

    def test_dry_run_detection(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        body = function(source, "bool GrindController::dry_run_detected(const GrindLoopData& loop_data)")
        code = r'''
#include <cassert>
#include <cstdint>
constexpr int GRIND_DRY_RUN_TIMEOUT_MS = 5000;
constexpr float GRIND_DRY_RUN_MIN_PROGRESS_G = 0.2f;
// unsigned long is 32 bits on the ESP32; use the same width on the host.
struct GrindLoopData { float current_weight; uint32_t now; };
struct GrindController {
    float dry_run_reference_weight_ = 0;
    uint32_t dry_run_reference_ms_ = 0;
    bool dry_run_detected(const GrindLoopData& loop_data);
};
''' + body + r'''
int main() {
    // An empty hopper gains nothing and is stopped after exactly 5 s.
    GrindController empty; empty.dry_run_reference_ms_ = 1000;
    assert(!empty.dry_run_detected({0.05f, 5999}));
    assert(empty.dry_run_detected({0.1f, 6000}));

    // A slow grinder (0.1 g/s) keeps making 0.2 g of progress within 5 s.
    GrindController slow; slow.dry_run_reference_ms_ = 0;
    for (uint32_t t = 0; t <= 60000; t += 20) {
        assert(!slow.dry_run_detected({0.1f * t / 1000.0f, t}));
    }

    // A hopper that runs dry part-way through is caught 5 s after the last gain.
    GrindController single_dose; single_dose.dry_run_reference_ms_ = 0;
    assert(!single_dose.dry_run_detected({12.0f, 4000}));
    assert(!single_dose.dry_run_detected({12.1f, 8999}));
    assert(single_dose.dry_run_detected({12.1f, 9000}));

    // The window is wrap-safe across the millis() rollover.
    GrindController wrap; wrap.dry_run_reference_ms_ = 0xFFFFF000u;
    assert(!wrap.dry_run_detected({0.0f, 0x00000100u}));
    assert(wrap.dry_run_detected({0.0f, 0xFFFFF000u + 5000u}));
}
'''
        compile_and_run(self, code)

    def test_guard_counts_samples_not_control_cycles(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        update = function(source, "void GrindController::update()")
        start = update.index("    bool vessel_removed = false;")
        end = update.index("    if (vessel_removed && phase == GrindPhase::FINAL_SETTLING)")
        guard = update[start:end]
        code = r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include "controllers/net_weight_guard.h"
struct GrindLoopData { float current_weight = 0; };
struct Sensor {
    float weight = 0; uint32_t stamp = 0;
    bool get_latest_sample(float* weight_out, uint32_t* stamp_out) const {
        *weight_out = weight; *stamp_out = stamp; return true;
    }
};
struct Controller {
    Sensor* weight_sensor;
    NetWeightRemovalGuard net_weight_removal_guard_;
    uint32_t last_guard_sample_ms_ = 0;
    bool cycle(bool net_weight_guard_active, const GrindLoopData& loop_data) {
''' + guard + r'''
        (void)guard_sample_weight;
        return vessel_removed;
    }
};
int main() {
    Sensor sensor; Controller c{&sensor}; c.net_weight_removal_guard_.reset(0.0f);
    GrindLoopData loop{};
    // One bad ADC sample seen by five 20 ms control cycles is one vote.
    sensor.weight = -400.0f; sensor.stamp = 100;
    for (int i = 0; i < 5; ++i) assert(!c.cycle(true, loop));
    sensor.weight = 1.0f; sensor.stamp = 200;
    for (int i = 0; i < 5; ++i) assert(!c.cycle(true, loop));
    // Three consecutive bad samples are a removal, on the third sample.
    for (uint32_t stamp : {300u, 400u}) {
        sensor.weight = -400.0f; sensor.stamp = stamp;
        for (int i = 0; i < 5; ++i) assert(!c.cycle(true, loop));
    }
    sensor.stamp = 500;
    assert(c.cycle(true, loop));
    // Outside guarded phases nothing is counted and partial counts reset.
    Controller idle{&sensor}; idle.net_weight_removal_guard_.reset(0.0f);
    for (uint32_t stamp : {600u, 700u}) { sensor.stamp = stamp; assert(!idle.cycle(true, loop)); }
    assert(!idle.cycle(false, loop));
    sensor.stamp = 800; assert(!idle.cycle(true, loop));
}
'''
        compile_and_run(self, code)


if __name__ == "__main__":
    unittest.main()
