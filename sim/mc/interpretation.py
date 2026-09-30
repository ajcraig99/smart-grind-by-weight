"""Static interpretation text for section 4 of the report.

These notes are the author's reading of the numbers in section 3 (observations). They are written against the full run
with the binary prefix below; run_mc.py prints a warning in the report when the binary differs, because the notes may then
no longer match the data. Every statement is labelled Interpretation, Uncertain or Assumption; none is a measurement of
real hardware.
"""

WRITTEN_AGAINST = "0bdb4e370a01"


def interpretation_text(quick, sha):
    L = []
    w = L.append
    w("## 4. Interpretation (reasoning from the observations; not measurements)")
    w("")
    if sha != WRITTEN_AGAINST:
        w("> **Warning:** these notes were written against the full run with `grindsim` binary prefix `%s`; this report was "
          "produced with `%s`. Re-read them against section 3 before relying on them." % (WRITTEN_AGAINST, sha))
        w("")
    if quick:
        w("> These notes refer to the full run; quick-run numbers are too small to check them.")
        w("")
    w("Labels: **Interpretation** = inference from the tables above, reasoning shown; **Uncertain** = needs checking before use; "
      "**Assumption** = taken as true to proceed. The plant parameters are placeholders, so every statement about magnitudes is "
      "about the model.")
    w("")
    w("### 4.1 Accuracy")
    w("")
    w("- **Interpretation (nominal):** the true-error distribution sits on the lower edge of the firmware band (mean about "
      "-0.03 g, section 3.2) while the firmware-reported error sits just inside it (mean about -0.02 g, 97 % in band). "
      "Reasoning: the two differ by a nearly constant offset (`fw - true` mean about +0.007 g, sd about 0.005 g), so a "
      "strict test on the true error splits the nominal runs roughly 60/40 even though the firmware sees almost all of them "
      "as in band. Most nominal failures are therefore small misses (60 % of runs between 0.03 and 0.05 g), not gross errors.")
    w("- **Uncertain:** the source of the +0.007 g offset between what the firmware displays and the plant's true cup mass. "
      "Candidates in the model are load-cell creep, zero drift and grounds that arrive after the reading is taken; none was "
      "isolated here. It is also not established whether it would exist on a real load cell.")
    w("- **Interpretation (purge keep and prime):** the firmware itself reports `OVERSHOOT` in most of these runs, with one "
      "pulse or none (section 3.4). Reasoning: pulses only add grounds, so once the predictive stage ends above the target the "
      "controller has no way to correct it; in the purge-discard case the controller approaches from below with 2 to 3 "
      "pulses. **Uncertain:** why the predictive stage ends high when the purge grounds stay in the cup; the cause "
      "(for example the stop-offset estimate in this mode) was not examined.")
    w("- **Interpretation (targets):** the error behaviour does not change visibly between 9 g and 40 g (section 3.7); the "
      "differences between targets are within what the sample size supports for runs that mostly sit on the band edge.")
    w("- **Interpretation (sweep):** the very high failure share in the sweep reflects that the sweep ranges are wide and "
      "drawn independently (**Assumption**: independent uniform draws, placeholders), not a real-world failure rate. The "
      "association tables (3.8) link `MAX_PULSES` most strongly with slower motor spin-up (`motor_tau_up_s`) and higher "
      "`motor_min_speed`; per `sim/plant/ASSUMPTIONS_PLANT.md` these two set the shortest productive pulse, so the "
      "reading is that coarser minimum pulses make fine corrections impossible (inference; the parameters were not varied "
      "one at a time). `ERROR 'No beans?'` runs had more mass spilled at the purge prompt and higher chute retention and flow "
      "parameters; in one inspected seed (100002) the 22 g supply ran out because the cup contents tipped out at the purge "
      "prompt were 4.4 g, well above the 1.0 g purge amount, and the firmware's dry-run rule fired "
      "(`[GRINDER] Dry run: under 0.2g gained in 5000ms of grinding`). That is one seed, not a class-wide finding.")
    w("")
    w("### 4.2 Single-dose run-dry")
    w("")
    w("- **Interpretation:** with an 18 g dose the model cannot deliver 18.0 g into the cup, because the burr chamber and chute "
      "keep grounds back (0.3 g and 0.25 g placeholders, the mean `burr_left_g` column in 3.6 is 0.30 g at the small doses), "
      "and with the purge discarded a further amount leaves with the cup contents. In-band results appear only at the larger "
      "doses in every purge mode (chart and table in 3.6). The exact thresholds are a consequence of the placeholders and "
      "will differ on a real grinder.")
    w("- **Interpretation:** between the dose that is clearly too small (firmware stops with `No beans?` via its dry-run rule) "
      "and the dose that is sufficient there is a band where the controller ends with `MAX_PULSES`: it keeps pulsing on an "
      "almost empty chamber and ends 0.04 to 0.2 g short. Reason (read from the source by the lead): the dry-run rule "
      "`dry_run_detected()` is only evaluated in the PRIME and PREDICTIVE phases (src/controllers/grind_controller.cpp, "
      "`update()`), not in the pulse phases, so a chamber that runs empty during corrections ends at the 10-pulse limit.")
    w("- **Interpretation:** when the burrs run empty the firmware reports an error rather than a false SUCCESS in every run "
      "of the 17.0 to 19.5 g (discard) cases; the user is told that the dose is short. In the run-dry set the smallest "
      "true error of any run with result `SUCCESS` was -0.056 g (no false SUCCESS with a large shortfall).")
    w("")
    w("### 4.3 HX711 sample rate")
    w("")
    w("- **Interpretation:** at 80 SPS the firmware cannot start (section 3.10), which matches the behaviour its troubleshooting "
      "text describes; the twin reproduces the documented refusal, so an 80 SPS variant cannot be evaluated without changing "
      "the firmware check. The 30 SPS set is a model-only way to see the effect of a faster cadence: pulse count and grind "
      "time drop slightly, and the error distribution is similar to the 10 SPS nominal set.")
    w("")
    w("### 4.4 Faults")
    w("")
    w("- **Interpretation (load cell disconnect):** the firmware ended every disconnect run with the `Scale disconnected` error. "
      "The SAFETY-SIGNAL tag on the runs where the motor was on at injection comes from an exceedance of 6 ms "
      "(`motor_invalid_signal_s` = 0.506 s against the 0.5 s threshold), i.e. the motor stopped about one freshness window "
      "after the disconnect. The tag is at the threshold; a threshold of 0.6 s would not tag these runs. The threshold is this "
      "report's definition from the brief, not a verdict on safety.")
    w("- **Interpretation (load cell stuck):** no run was ended by a stuck-sensor detector; the motor ran with a frozen signal "
      "for up to about 5 s (`motor_invalid_signal_s`) and the runs ended through the dry-run rule (`No beans?`) or the pulse "
      "limit. In the PULSE_DECISION case the cup was overfilled by 1.1 to 1.4 g while the firmware reported a final weight "
      "near target and ended with `MAX_PULSES`. The source has no stuck-value check (read by the lead): freshness "
      "`WeightSensor::has_recent_sample()` only tests that samples keep arriving, and the dry-run rule is the only "
      "progress check (sim/FINDINGS.md F3). The UI text `No beans?` was shown for a "
      "sensor fault, which a user could misread (Interpretation of the observed text only).")
    w("- **Interpretation (relay stuck on):** the firmware cannot open a welded relay; the twin shows the plant motor running "
      "while the firmware ended the session with `OVERSHOOT` (cup overfilled by 1.5 to 1.8 g in every mid-grind injection) or "
      "with an error or timeout when injected before or during the purge. `motor_after_end_s` is about 3.1 s in every case **only because the scripted operator dismisses the grind "
      "3 s after the result**; the plant fault persists, so the real duration is unbounded in the model. "
      "Engineering judgement: a fault that leaves the motor energised needs protection outside the firmware "
      "(for example a separate contactor or thermal cut-out); that is a design observation, not something this twin shows "
      "the firmware could fix. With the relay stuck from START the tare never settled (`Timeout:TARE`) in about half the runs.")
    w("- **Interpretation (relay stuck off, motor stall):** with no grounds arriving, the firmware ended with `No beans?` (or "
      "`MAX_PULSES` when injected at PULSE_DECISION); no SAFETY tag. The same text is shown for an empty hopper, a dead relay "
      "and a jammed burr, so the UI does not distinguish these causes (observation of the text, interpretation that it could "
      "mislead).")
    w("- **Interpretation (cup removed):** the firmware ended the grind with `Err: neg wt` when the cup was lifted mid-grind, "
      "with no SAFETY tag. When the cup was lifted during the purge prompt sequence "
      "(`rnd1`, 7.3 s after START) the firmware log of seed 600000 shows `Purge continue held: -99.94g suggests the vessel "
      "is off`, and the scripted operator (which never replaces that cup) gave up after 150 s of virtual time; this row "
      "reflects the scripted operator's limit as much as the firmware.")
    w("- **Interpretation (bump):** the outcome depends on when the bump lands. A 200 g bump at PREDICTIVE+0.5, +2 or +5 s or "
      "at START+11.3 s, and a 30 g bump at PREDICTIVE+5 s, ended with `MAX_PULSES` and a cup 2.6 to 12.1 g short (table 3.9); "
      "a 30 g bump at PREDICTIVE+0.5 or +2 s, and both sizes at PULSE_DECISION+0, START+7.3 s and START+20.8 s, matched the "
      "unperturbed mix; at START+17.4 s 3 of 20 runs ended `MAX_PULSES` and 2 `OVERSHOOT` for both sizes. `MAX_PULSES` counts as a completed grind (`is_completed_grind_result`, "
      "src/controllers/grind_session_result.h) and the UI shows the completion screen, so the user is not told that the cup "
      "is short. Mechanism (traced by the lead, sim/FINDINGS.md F4): the transient inflates the 1.5 s flow estimate, the "
      "predictive stop offset is computed from it without the 1-3 g/s clamp the pulse path applies, and the 50 ms weight "
      "used for the stop test can satisfy it; the motor stops early and 10 pulses cannot make up the difference.")
    w("- **Interpretation (noise burst):** a x20 noise burst of 1 s left the outcome mix at the 17-under / 3-in-band split of the "
      "unperturbed fault seeds (the feed-block-after-START rows are a control, see below) in most timings, so the burst was "
      "absorbed; permanent x20 noise produced more pulses and more `OVERSHOOT` results but no SAFETY tag.")
    w("- **Interpretation (feed block):** a block injected after START had no effect in the model because the whole single dose "
      "has already fallen into the burr chamber (plant assumption M4 in `sim/plant/ASSUMPTIONS_PLANT.md`: feed 30 g/s into a "
      "25 g chamber); only the `boot0` injection, before the beans are loaded, produces a failure (`No beans?`). This "
      "says nothing about bridging in a real hopper.")
    w("- **Interpretation (reset):** the firmware rebooted to READY in every reset run and did not energise the relay "
      "after the reboot (longest relay-closed time after reboot 0.007 s); no grind state was resumed. The cup "
      "keeps the partial grounds (table after 3.9).")
    w("")
    return "\n".join(L)
