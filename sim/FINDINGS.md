# Findings

Observed firmware behaviour in the digital twin. Each finding gives the observation (what the twin
recorded, with the command to reproduce it) separately from the interpretation. SAFETY marks cases
where the motor ran without a valid weight signal or beyond a sane time limit (definitions in
`sim/reports/montecarlo.md`). Findings only; the firmware was not changed.

Scope and limits (applies to every finding): the firmware is the real code (commit 3430179), but the
grinder, grounds path and load cell are a model. 32 of 38 plant parameters are placeholders
(`sim/plant/ASSUMPTIONS_PLANT.md`), so magnitudes (grams, seconds) depend on them. The logic paths the
firmware takes are real; how often they occur on a real grinder is not established here.
All commands run from the repository root after building (`sim/README.md`); seeds are deterministic.
Statistics over many seeds and parameter sweeps are in `sim/reports/montecarlo.md`.

## F1 - An 18 g single dose cannot reach an 18.0 g target on the first grind (Purge mode)

Observation: `sim/out/host/grindsim --seed 3 --scenario sim/scenarios/findings/dose18.json`
(18.0 g loaded, default Purge mode, user tips the purge out) ends TIMEOUT / ERROR "No beans?" with
15.60 g shown and 15.63 g in the cup; 1.82 g went to the purge. With the purge kept
(`findings/keep.json`) or Prime mode (`findings/prime.json`) it also ends "No beans?" at 17.48 g.

Interpretation: the purge of the first grind after boot (grounds are "stale", `grounds_are_stale()`)
takes about 1 g plus coast (measured 1.8 g), and some grounds stay in the chute and burrs. With a
single dose equal to the target there is nothing left to reach the target, so the dry-run detector
ends the grind. How much is retained depends on placeholder parameters (`chute_retention_g` 0.25 g,
`burr_residual_g` 0.3 g); the purge amount itself comes from the firmware
(GRIND_PURGE_AMOUNT_DEFAULT_G 1.0 g) and the model's coast. The "No beans?" message is the firmware's
dry-run path working as designed; the user-facing consequence is that single-dosing the exact target
weight is not compatible with Purge or Prime, at least in the model.

## F2 - Relay welded on: the session "completes", the UI returns to ready, the motor keeps running (SAFETY-TIME)

Observation: `grindsim --seed 3 --scenario sim/scenarios/findings/relayon.json --out run.csv`
(relay contacts stick closed 3 s into PREDICTIVE) ends COMPLETED / OVERSHOOT with 19.65 g. The relay
contact is still closed and the motor at full speed at the end of the run (38.9 s), with the controller
in IDLE and the UI on the ready screen; 3.1 s of motor time after the session ended were counted in
the summary before the operator dismissed the result, and the motor never stops.

Interpretation: the firmware has no feedback on the actual motor or relay state and does not watch for
weight still rising after it stopped the motor, so it cannot detect or report a welded relay. The
dead-man timer only drives the pin low, which does not help when the contacts are welded. A welded
relay is a hardware fault outside firmware control; the observation is that nothing warns the user.

## F3 - A stuck load-cell reading is not detected (SAFETY-SIGNAL)

Observation: `grindsim --seed 3 --scenario sim/scenarios/findings/stuck.json` (HX711 keeps returning
the same code 3 s into PREDICTIVE): the motor runs 4.93 s on the frozen reading, then the dry-run
detector ends the grind with "No beans?"; the UI shows 4.85 g while 14.80 g are in the cup.
`findings/stuckpulse.json` (stuck from the first PULSE_DECISION): all 10 correction pulses run
against the frozen reading (2.97 s of motor time on an invalid signal), the session ends
COMPLETED / MAX_PULSES showing 17.53 g while 19.30 g are in the cup (+1.3 g unreported overshoot).

Interpretation: freshness (`has_recent_sample`) only checks that samples arrive, not that they change.
The dry-run detector bounds a stuck reading during continuous grinding to about 5 s, but pulses are not
covered by it. Whether a real HX711 can freeze like this (as opposed to disconnecting) is not
established; the case models a stuck ADC or a mechanically jammed load cell.

## F4 - A single bump on the cup during grinding can end the grind far short (MAX_PULSES at 11 g)

Observation: `grindsim --seed 3 --scenario sim/scenarios/findings/bump.json --out run.csv`
(200 g-peak transient on the platform 3 s into PREDICTIVE): the firmware's low-latency weight reads
61.7 g for one sample, the predictive stop offset (`motor_stop_target_weight`) jumps to 23.7 g, the
motor stops at 5.8 g, and 10 correction pulses bring it to 10.99 g. The session ends
COMPLETED / MAX_PULSES with 10.98 g in the cup against an 18.0 g target.

Interpretation: `run_predictive_phase` (src/controllers/weight_grind_strategy.cpp) recomputes the stop
offset from the 1.5 s flow rate without the 1-3 g/s sanity clamp that the pulse path applies
(`get_clamped_pulse_flow_rate`), and the stop test uses a 50 ms-window weight, so one spike both
inflates the offset and can satisfy the stop test. The bump magnitude is a model input; the Monte Carlo
report also runs a 30 g bump. Ending "complete" with 61 % of the dose may not be what a user expects
from a MAX_PULSES result.

## F5 - HX711 at 80 SPS is refused at boot

Observation: `grindsim --seed 3 --scenario sim/scenarios/normal.json --set hx711_sps=80 --log log.txt`:
the log shows "HX711 sample rate detected at 47.6 SPS (expected ~10 SPS)", the scale reports
INVALID_SAMPLE_RATE and no grind can start (operator status "boot timeout").

Interpretation: consistent with the documented design (docs/TROUBLESHOOTING.md, 80 SPS blocks
startup). Detected rate is 47.6, not 80, because the sampling task polls every 20 ms, so it can read at
most 50 samples per second; the >40 SPS threshold still catches it.

## F6 - Load-cell unplugged mid-grind: motor stops after the 500 ms freshness window

Observation: `grindsim --seed 3 --scenario sim/scenarios/findings/lcdisc.json`: TIMEOUT /
SCALE_ERROR "Scale disconnected"; the motor ran 0.51 s after the disconnect.

Interpretation: the freshness guard works as designed; 0.5 s of blind motor time is the design window.

## F7 - Cup removed mid-grind: stopped in 0.3 s ("Err: neg wt")

Observation: `grindsim --seed 3 --scenario sim/scenarios/findings/cupoff.json`: TIMEOUT / ERROR
"Err: neg wt" 0.31 s after the cup was lifted (3 consecutive samples below -0.9 x the 100 g cup).

Interpretation: the net-weight removal guard works as designed for a cup placed after boot.

## F8 - Motor stall, relay stuck open: ended by the dry-run detector after 5 s

Observation: `findings/stall.json` (burrs jam 3 s into PREDICTIVE) and `findings/roff.json` (relay
never closes) both end "No beans?" 5 s after progress stops.

Interpretation: the firmware cannot tell a jam, an open relay and an empty hopper apart; all three show
the same message. The motor (if powered) stays energised against the jam for those 5 s.

## F9 - Reset mid-grind: the session is lost, the motor stops, no grind resumes

Observation: `grindsim --seed 3 --scenario sim/scenarios/findings/reset.json` (power cut 3 s into
PREDICTIVE, then a fresh boot with NVS and flash kept): the relay drops at the reset, the firmware boots
to the ready screen, does not restart the motor, and shows no record of the interrupted grind;
5.59 g stay in the cup.

Interpretation: grind state is RAM-only, so nothing resumes after a reset (which is the safe outcome).
The interrupted session is not written to history.

## F10 - Load-cell noise burst: completes, 0.11 g over, 49 s

Observation: `findings/noise.json` (noise x20 from 3 s into PREDICTIVE): COMPLETED / OVERSHOOT at
18.11 g shown (18.02 g true), 7 pulses, 49.3 s, close to the 60 s timeout.

Interpretation: settling never succeeds under that noise, so every pulse waits for its 3 s settling
timeout and decides on the smoothed weight.
