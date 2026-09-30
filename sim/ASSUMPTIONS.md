# Assumptions and placeholders

Every plant-model default that is not cited from upstream code or docs is listed here with its
rationale and the sweep range used by the Monte Carlo harness. The authoritative per-parameter
table (name, unit, default, sweep range, source) is generated from `sim/plant/plant_params.c`
into `sim/plant/PARAMS.md`.

## Runtime and shim assumptions

| ID | Assumption | Rationale | Effect if wrong |
|----|------------|-----------|-----------------|
| R1 | Both ESP32 cores are serialised on one cooperative scheduler; firmware code takes zero virtual time. | Determinism; single-threaded WASM. | Races between cores are not reproduced. |
| R2 | FreeRTOS tick 1 ms. | Verified: CONFIG_FREERTOS_HZ 1000 in framework-arduinoespressif32-libs 3.3.2 esp32s3/qio_opi/include/sdkconfig.h:1178 (downloaded by the pio test). | - |
| R3 | Arduino loopTask priority 1 on core 1. | Verified: framework-arduinoespressif32 3.3.2 cores/esp32/main.cpp:113. | - |
| R4 | Panel byte order: resolved from source, no longer an assumption. LVGL 9.5 swaps RGB565 bytes before flush when LV_COLOR_16_SWAP is set (lvgl src/core/lv_refr.c:1433-1434; include/lv_conf.h:34 sets it for the device), and Arduino_GFX 1.6.7 `draw16bitBeRGBBitmap` sends the bytes unchanged. The virtual panel decodes that path as big-endian. | Verified against the library sources fetched from GitHub. | None expected. |
| R5 | FT3168 register 0x02 read returns [count, xh, xl, yh, yl]; the driver masks count with 0x0F and coordinates with 0x0F high nibbles. | Matches src/hardware/touch_driver.cpp:98-102. | None for the firmware; the shim encodes what the driver decodes. |
