# Build List (revised)

**Status:** revised 2026-09-27 for this fork, which is based on the community fork `Clinteastman/smart-grind-by-weight` at `b4a0be6`.

**Based on:**
- the project parts list in [HARDWARE_INSTALLATION.md](HARDWARE_INSTALLATION.md);
- the hardware recommendations in [CODE_REVIEW.md](CODE_REVIEW.md) section 3.

**Assumption:** the grinder is a Eureka Mignon Specialita, the reference build. For other models, check [GRINDER_COMPATIBILITY.md](GRINDER_COMPATIBILITY.md) first; some need extra parts (a 5 V buck or a mains relay).

**About the links (read before ordering):**
- All links were found by web search on 2026-09-27, AliExpress preferred. **None was opened**, because AliExpress is blocked from the environment the search ran in.
- "Matches" means the listing's title, as shown in search results, states that spec.
- Listings change or disappear, and one listing often holds several variants. Before ordering, check the photos and pick the right variant: capacity, size, pin count, gender.
- Links on regional subdomains (de., fr., ja., es.) were normalised to `www.aliexpress.com` with the same item ID. `aliexpress.us` links are kept as returned, because they use a different ID scheme. Tracking query strings were removed.
- Prices are not given; they vary by region and change often.
- "Project link" means a link already in [HARDWARE_INSTALLATION.md](HARDWARE_INSTALLATION.md). Several of those didn't appear in search results and may be dead.

---

## Before you order

1. **Board revision.** Waveshare has shipped two incompatible generations of the 1.64" board. This fork supports both, with separate firmware images. No listing found says which revision it sells, so identify the board on arrival: [TROUBLESHOOTING.md](TROUBLESHOOTING.md#display-stays-black-after-flashing-waveshare-164-v2). The wiring differs; see [Wiring summary](#wiring-summary-by-board-revision).
2. **The HX711 must run at 10 samples/s.** The firmware refuses 80 SPS and reports `HX711_SAMPLE_RATE_INVALID` at startup. The RATE pin must be tied to GND. None of the listings below says how RATE is set [TO CONFIRM on arrival]. If you get that error, the module is set to 80 SPS; look for a rate jumper or the RATE pin on the board.
3. **Dosing cup size.** Pick 54 mm or 58 mm so it matches the cup holder you print.

---

## 1. Core electronics

**1. Waveshare ESP32-S3-Touch-AMOLED-1.64** (qty 1)
- Spec: 1.64" 280×456 AMOLED with touch. Either revision works with this fork.
- Links:
  - https://www.aliexpress.us/item/3256808750078935.html: title names Waveshare, 1.64", 280×456.
  - https://www.aliexpress.com/item/1005009056849529.html: title matches Waveshare's product wording; store not confirmed.
  - Official product page: https://www.waveshare.com/esp32-s3-touch-amoled-1.64.htm

**2. HX711 load-cell amplifier module** (qty 2, one is a spare)
- Spec: 10 SPS (RATE to GND). Shielded (metal can) preferred.
- Links:
  - https://www.aliexpress.com/item/32959403751.html: shielded, per its title.
  - https://www.aliexpress.com/item/32826443159.html: 10-pack, shielded.
  - Project link: https://nl.aliexpress.com/item/1005006851380544.html (not found in search).

**3. Load cell, T70 bar, 1 kg** (qty 1)
- Spec:
  - 70 × 22 × 15 mm (L × H × D), as the project parts list requires;
  - 4 holes in a rectangular 2×2 pattern (not 4 in a line);
  - shielded 5-wire cable.
- The 1 kg T70 is the cell the original author verified, and it suits a portafilter.
- **No listing was found whose title confirms all of this,** so check the photos or drawing.
- Links:
  - https://www.aliexpress.com/item/1005005923073787.html: "T70" single point; pick the 1 kg variant. May be dead.
  - Project links: https://nl.aliexpress.com/item/1005008658337192.html (T70 1 kg) and https://nl.aliexpress.com/item/1005009409460619.html (T70). Neither was found in search.
- Alternatives:
  - P70 1 kg, which the project notes "looks compatible, not tested": https://www.aliexpress.com/item/1005006257978435.html (may be dead).
  - 0.3 kg, for a dosing cup only: Mavin NA6 at https://www.aliexpress.us/item/3256805397929823.html (a 300 g variant is listed).

**4. Electrolytic capacitor, 1000 µF, 16 V** (10 V minimum; qty 1)
- Goes across the 5 V and GND pins against brownouts. A small can fits more easily inside the housing.
- Links:
  - https://www.aliexpress.com/item/1005006048291139.html: 10-pack, 16 V, 8×16 mm, low ESR.
  - Project link: https://www.aliexpress.com/item/1005006037906723.html: per its title, Rubycon YXF, 10 V, 10×16 mm.

**5. New: 10 kΩ resistor, 1/4 W** (qty 1)
- Pull-down from the motor-control line to GND, so the motor stays off during reset, boot, flashing and brownout (review section 3.2).
- Link: https://www.aliexpress.us/item/3256805669511488.html: 1/4 W 1% metal-film kit whose title lists 10K, 220 and 470 Ω. It also covers item 6.

**6. New (optional): 330-470 Ω resistor, 1/4 W** (qty 1)
- In series between the GPIO and the grinder's motor lead, to limit current into the ESP32 pin.
- Link: same kit as item 5.

**7. New (optional): 100 nF ceramic capacitor** (qty 1)
- Across 5 V and GND close to the board, next to the 1000 µF capacitor.
- Links:
  - https://www.aliexpress.com/item/32429917283.html: 100 × 0.1 µF, 50 V.
  - https://www.aliexpress.us/item/3256803304676025.html: assortment including 100 nF.

## 2. Wiring and connectors

**8. 22 AWG silicone wire, several colours** (1 set)
- Lengths: load cell → HX711 ~10 cm; Eureka → board ~15 cm; harness → HX711 ~30 cm.
- Links:
  - https://www.aliexpress.com/item/1005012312424560.html: 22 AWG, 5 colours.
  - Project link: https://www.aliexpress.com/item/2255800441309579.html (choose the gauge).

**9a. Dupont 2.54 mm housing and pin kit, plus a crimp tool** (1 each; only if you want to crimp your own)
- Kit: https://www.aliexpress.com/item/4000566842856.html: 620 pcs, male and female pins.
- Crimper: https://www.aliexpress.com/item/1891683351.html: IWISS SN-28B, AWG 28-18.
- 22 AWG silicone insulation may be too thick for Dupont insulation grips (Uncertain), so crimp thinner wire (24-26 AWG) or use 9b.
- The project's "Dupont kit" link is, per its title, an SN-58B crimper plus a terminal set.

**9b. Or: pre-crimped female-female Dupont jumpers, 10-20 cm** (1 pack)
- No crimping needed; cut and solder one end.
- Link: https://www.aliexpress.com/item/1847754499.html: 40 pcs, 20 cm.

**10. Right-angle 2.54 mm male pin headers** (1 strip)
- For the HX711 (VCC, GND, DOUT, SCK).
- Link: https://www.aliexpress.com/item/1005006149080284.html: the project link; right-angle 1×40.

**11. JST-PH 2.0 mm 4-pin male pigtail** (optional, qty 1)
- Solder-free connection to the Eureka harness. Check the gender against your plug before cutting anything.
- Link: https://www.aliexpress.com/item/4000386468665.html: a pre-wired male + female pair, 20 cm, 26 AWG.
- The project link is, per its title, a 230-piece crimp kit, not a pigtail.

**12. New: heat-shrink tubing assortment** (qty 1)
- Insulate the unused Pin 2 (button) lead and the resistor joints.
- Link: https://www.aliexpress.us/item/3256806974390856.html: 2:1 assortment.

## 3. Mechanical and printed parts

**13. M3 screws, about 10 mm** (qty 6)
- Head type [TO CONFIRM against the printed parts].
- Link: https://www.aliexpress.com/item/32334431524.html: 90-piece M3 socket-head set, M3×4 to M3×30, stainless 304.

**14. Dosing cup, 54 mm or 58 mm** (qty 1)
- Must match the cup holder.
- Links:
  - https://www.aliexpress.com/item/1005006487660144.html: stainless, 51/54/58 mm variants.
  - Project link (54 mm): https://nl.aliexpress.com/item/1005006526852408.html (not found in search).

**15. 3D-printed parts, in PETG** (1 set)
- Screen adapter, back plate, cover, cup holder (54 or 58 mm), and hole covers.
- STLs are in `3d_files/`; details in [HARDWARE_INSTALLATION.md](HARDWARE_INSTALLATION.md#3d-printed-parts).
- Print these yourself or use a print service.

## 4. Tools and test equipment

**16. Multimeter** (your own)
- Required: confirm the Eureka 5 V pin, and identify the motor lead by position, not colour.

**17. Soldering iron and solder** (your own)
- For the load cell to HX711, the headers and the resistor.

**18. USB-C data cable** (your own)
- For the first flash over USB.

**19. New: USB-C inline power meter**
- Measures the board's current (BLE on, full brightness) against the Eureka's roughly 100 mA of 5 V headroom (review 3.3).
- Link: https://www.aliexpress.com/item/1005004683542341.html: FNIRSI FNB58.
- FNIRSI store: https://fnirsi.aliexpress.com/store/group/FNIRSI-USB-Tester/2939001_516831887.html

**20. New: 24 MHz 8-channel USB logic analyser**
- Checks motor pulse lengths and the motor pin at boot (review F-06 and F-33).
- Link: https://www.aliexpress.com/item/1005005680498612.html: NanoDLA; its title names sigrok/PulseView (free software).

**21. New (optional): 100 g or 200 g calibration weight**
- More accurate than a mug of water.
- Links:
  - https://www.aliexpress.com/item/1005006499871417.html: M1 class, 50-500 g, hook type.
  - https://www.aliexpress.us/item/3256806517728085.html: 1 mg-200 g set, M1/F1 by variant (whether a 100 g piece is included isn't stated).

## 5. Later upgrade (don't buy yet)

**NAU7802 24-bit load-cell ADC breakout**
- Why later:
  - faster sampling (10-320 SPS) over I2C instead of bit-banging;
  - this fork has no driver for it yet (review 3.4);
  - decide after measuring noise on the first build.
- Links:
  - https://www.aliexpress.com/item/1005005776009064.html: its title reads "4538 NAU7802 … STEMMA QT / Qwiic". 4538 is Adafruit's product number, so it may be the original or a copy.
  - Originals: https://www.adafruit.com/product/4538 and https://www.sparkfun.com/sparkfun-qwiic-scale-nau7802.html

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
2. Bench-test the board, HX711 and load cell on USB power before connecting the motor lead. The startup log will show `HX711_SAMPLE_RATE_INVALID` if the HX711 is set to 80 SPS.
3. With the grinder unplugged, identify Pin 1 (5 V) and Pin 3 (motor) with the multimeter.
4. With the pull-down fitted and the logic analyser on the motor GPIO, power-cycle the board on the bench. The line should stay low throughout boot.
