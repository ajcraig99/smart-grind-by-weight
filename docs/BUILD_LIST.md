# Build List (revised)

**Status:** revised 2026-09-27 for this fork, which is based on the community fork `Clinteastman/smart-grind-by-weight` at `b4a0be6`.

**Based on:**
- the project parts list in [HARDWARE_INSTALLATION.md](HARDWARE_INSTALLATION.md);
- the hardware recommendations in [CODE_REVIEW.md](CODE_REVIEW.md) section 3.

**Assumption:** the grinder is a Eureka Mignon Specialita, the reference build. For other models, check [GRINDER_COMPATIBILITY.md](GRINDER_COMPATIBILITY.md) first; some need extra parts (a 5 V buck or a mains relay).

**Links:** pending. `LINK-n` cells are placeholders until the link search finishes. Links come from web search on 2026-09-27, AliExpress preferred.
- AliExpress is blocked from the environment the search was run in, so **no listing was opened**.
- Before ordering, check that each one still exists and that you pick the right variant: capacity, pin count, size.
- Prices are not given; they vary by region and change often.

---

## Before you order

1. **Board revision.** Waveshare has shipped two incompatible generations of the 1.64" board. This fork supports both, with separate firmware images. You can't reliably choose from a listing, so identify the board on arrival: [TROUBLESHOOTING.md](TROUBLESHOOTING.md#display-stays-black-after-flashing-waveshare-164-v2). The wiring differs; see [Wiring summary](#wiring-summary-by-board-revision).
2. **The HX711 must run at 10 samples/s.** The firmware rejects 80 SPS: the module's RATE pin must be tied to GND. Most cheap green modules are hard-wired to 10 SPS, but check the photo or the listing.
3. **Dosing cup size.** Pick 54 mm or 58 mm so it matches the cup holder you print.

---

## 1. Core electronics

| # | Part | Qty | Spec and notes | Link |
|---|---|---|---|---|
| 1 | Waveshare ESP32-S3-Touch-AMOLED-1.64 | 1 | 280×456 AMOLED with touch. Either revision works with this fork. | LINK-1 |
| 2 | HX711 load-cell amplifier module | 2 (1 spare) | 10 SPS (RATE to GND). A shielded version is preferred. | LINK-2 |
| 3 | Load cell, T70 bar, **1 kg** | 1 | 70 × 22 × 15 mm (L × H × D), as the project parts list requires. 4 holes in a rectangular 2×2 pattern (not 4 in a line). Shielded 5-wire cable. The 1 kg T70 is the cell the original author verified; it also suits a portafilter. | LINK-3 |
| 4 | Electrolytic capacitor, 1000 µF, 16 V (10 V minimum) | 1 | Goes across the 5 V and GND pins against brownouts. Pick a small can so it fits inside the housing. | LINK-4 |
| 5 | **New:** 10 kΩ resistor, 1/4 W | 1 | Pull-down from the motor-control line to GND, so the motor stays off during reset, boot, flashing and brownout (review section 3.2). | LINK-5 |
| 6 | **New (optional):** 330-470 Ω resistor, 1/4 W | 1 | In series between the GPIO and the grinder's motor lead, to limit current into the ESP32 pin. Items 5 and 6 come in the same assortment kit. | LINK-5 |
| 7 | **New (optional):** 100 nF ceramic capacitor | 1 | Across 5 V and GND close to the board, next to the 1000 µF capacitor. | LINK-6 |

## 2. Wiring and connectors

| # | Part | Qty | Spec and notes | Link |
|---|---|---|---|---|
| 8 | 22 AWG silicone wire, several colours | 1 set | Lengths: load cell → HX711 ~10 cm; Eureka → board ~15 cm; harness → HX711 ~30 cm. | LINK-7 |
| 9a | Dupont 2.54 mm connector kit **and** a crimp tool | 1 each | Only if you want to crimp your own. | LINK-8 |
| 9b | Or: pre-crimped female-female Dupont jumpers, 10-20 cm | 1 pack | No crimping needed; cut and solder one end. | LINK-9 |
| 10 | Right-angle 2.54 mm male pin headers | 1 strip | For the HX711 (VCC, GND, DOUT, SCK). | LINK-10 |
| 11 | JST-PH 2.0 mm 4-pin male pigtail (optional) | 1 | Solder-free connection to the Eureka harness. | LINK-11 |
| 12 | **New:** heat-shrink tubing assortment | 1 | Insulate the unused Pin 2 (button) lead and the resistor joints. | LINK-12 |

## 3. Mechanical and printed parts

| # | Part | Qty | Spec and notes | Link |
|---|---|---|---|---|
| 13 | M3 screws, about 10 mm | 6 | Head type [TO CONFIRM against the printed parts]. An M3 assortment is the easy option. | LINK-13 |
| 14 | Dosing cup, 54 mm or 58 mm | 1 | Must match the cup holder. | LINK-14 |
| 15 | 3D-printed parts, in PETG | 1 set | Screen adapter, back plate, cover, cup holder (54 or 58 mm), hole covers. STLs are in `3d_files/`; details in [HARDWARE_INSTALLATION.md](HARDWARE_INSTALLATION.md#3d-printed-parts). | print |

## 4. Tools and test equipment

| # | Item | Why | Link |
|---|---|---|---|
| 16 | Multimeter | Required: confirm the Eureka 5 V pin and identify the motor lead by position, not colour. | own |
| 17 | Soldering iron and solder | Load cell to HX711, headers, resistor. | own |
| 18 | USB-C **data** cable | First flash over USB. | own |
| 19 | **New:** USB-C inline power meter | Measure the board's current (BLE on, full brightness) against the Eureka's roughly 100 mA 5 V headroom (review 3.3). | LINK-15 |
| 20 | **New:** 24 MHz 8-channel USB logic analyser | Check motor pulse lengths and the motor pin at boot (review F-06 and F-33). Works with the free sigrok/PulseView software. | LINK-16 |
| 21 | **New (optional):** 100 g / 200 g calibration weight | More accurate than a mug of water for calibration. | LINK-17 |

## 5. Later upgrade (don't buy yet)

| Part | Why later | Link |
|---|---|---|
| NAU7802 24-bit load-cell ADC breakout | Faster sampling (10-320 SPS) over I2C instead of bit-banging. This fork has no driver for it yet; see review 3.4. Decide after measuring noise on the first build. | LINK-18 |

---

## Wiring summary by board revision

From [HARDWARE_INSTALLATION.md](HARDWARE_INSTALLATION.md#installation-and-wiring), plus the new pull-down.

| Signal | Original board (V1) | Newer board (V2) |
|---|---|---|
| HX711 SCK | GPIO 2 | GPIO 1 |
| HX711 DOUT | GPIO 3 | GPIO 3 |
| HX711 VCC / GND | 3.3 V / GND | 3.3 V / GND |
| Motor control → Eureka Pin 3 | GPIO 18 | GPIO 16 (**never GPIO 18** on V2: it's the touch interrupt) |
| 5 V → Eureka Pin 1, GND → Eureka Pin 4 | yes | yes |
| Eureka Pin 2 (button) | not used; insulate | not used; insulate |
| **New:** 10 kΩ pull-down | motor GPIO to GND | motor GPIO to GND |
| **New (optional):** 330-470 Ω series resistor | between the motor GPIO and Pin 3 | between the motor GPIO and Pin 3 |
| 1000 µF capacitor (+ optional 100 nF) | across 5 V and GND | across 5 V and GND |

**Load cell to HX711:** Red E+, Black E-, White A-, Green A+, Yellow (shield) to HX711 GND.

## On arrival

1. Flash the right image and confirm the display works before wiring anything ([FIRMWARE_SETUP.md](FIRMWARE_SETUP.md)).
2. Bench-test the board, HX711 and load cell on USB power before connecting the motor lead.
3. With the grinder unplugged, identify Pin 1 (5 V) and Pin 3 (motor) with the multimeter.
4. With the logic analyser on the motor GPIO, power-cycle the board. The line should stay low throughout boot.
