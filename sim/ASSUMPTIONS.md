# Assumptions and placeholders

Every plant-model default that is not cited from upstream code or docs is listed here with its
rationale and the sweep range used by the Monte Carlo harness. The authoritative per-parameter
table (name, unit, default, sweep range, source) is generated from `sim/plant/plant_params.c`
into `sim/plant/PARAMS.md`.

## Runtime and shim assumptions

| ID | Assumption | Rationale | Effect if wrong |
|----|------------|-----------|-----------------|
| R1 | Both ESP32 cores are serialised on one cooperative scheduler; firmware code takes zero virtual time. | Determinism; single-threaded WASM. | Races between cores are not reproduced. |
| R2 | FreeRTOS tick 1 ms (CONFIG_FREERTOS_HZ 1000 on Arduino-ESP32). | Arduino-ESP32 default [TO CONFIRM against the pioarduino 55.03.32 sdkconfig]. | Delay rounding differs. |
| R3 | Arduino loopTask priority 1 on core 1. | Arduino-ESP32 default [TO CONFIRM]. | Ordering of loop() versus tasks. |
| R4 | The panel shows exactly what LVGL renders (RGB565 native order); `draw16bitBeRGBBitmap` byte order is not reproduced. | Colour correctness on the real panel is a hardware property outside the twin. | Virtual screen colours could differ from the device if the real path swaps bytes. |
| R5 | FT3168 register 0x02 read returns [count, xh, xl, yh, yl]; the driver masks count with 0x0F and coordinates with 0x0F high nibbles. | Matches src/hardware/touch_driver.cpp:98-102. | None for the firmware; the shim encodes what the driver decodes. |
