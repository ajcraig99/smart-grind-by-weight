# Review Findings: Status on This Branch

This page tracks every finding in [CODE_REVIEW.md](CODE_REVIEW.md) on branch
`claude/code-review-ui-improvements-9e89pk`, which is based on the community
fork at `b4a0be6`. The review's file:line references are to upstream
`afdacc8` and are not repeated here.

**Status values:**
- **Fork:** already fixed in the community fork before this branch.
- **Fixed:** fixed on this branch, in the commit listed.
- **Partly:** the main risk is fixed; the note says what remains.
- **Open:** not changed yet, with the reason.

**Commits on this branch:**

| Commit | Scope |
|---|---|
| `f3df959` | Network and Bluetooth access control |
| `c9b4665` | Firmware rollback and stalled-update recovery |
| `2ee7384` | Motor dead-man, motor pin LOW at boot, `-O2` |
| `67dc45e` | Scale buffer thread safety, settling, tare, calibration |
| `1c3c6c7` | Grind control: dry run, purge re-tare, removal guard, timeouts |
| `09b9e57` | Touchscreen fixes |
| `a13c928` | Dose limit of 40 g |
| `e35bd56` | Touchscreen guard gaps; removal threshold after the purge re-tare |
| `c40f5a2` | Follow-up review: purge prompt, settling, tare, motor safety stop |
| `fba0c68` | Follow-up review: request admission, update confirmation, rollback reporting |

Verification on this branch: host tests (`tools/tests`) and V1 and V2 firmware
builds. **Nothing here has been tested on hardware.** The bench checks at the
end of this page are needed before first use.

## Firmware findings

| ID | Finding | Status | Where | Notes |
|---|---|---|---|---|
| F-01 | STOP races the control loop | Fork | | Controller mutex |
| F-02 | Bluetooth update mid-grind | Fork, then Fixed | `c9b4665` | Fork refuses it; a silent client is now aborted after 30 s |
| F-03 | Stale or failed scale mid-grind | Fork | | Stops after 500 ms without a valid sample |
| F-04 | Window buffer overflow | Fork | | |
| F-05 | Continuous run is an endless RMT loop | Fixed | `2ee7384` | Still an endless loop, now cut off by a 1 s dead-man |
| F-06 | Pulse length encoding | Fork | | |
| F-07 | Purge amount key too long | Fork | | |
| F-08 | Single-sample "Err: neg wt" | Fork, then Fixed | `1c3c6c7` | See N-B2 and N-B3 |
| F-09 | Unauthenticated Bluetooth update | Partly | `f3df959` | Needs **Allow Update** on the grinder; the link itself is still unpaired |
| F-10 | Export takes over the screen mid-grind | Fixed | `f3df959` | Transfers refused while active |
| F-11 | No re-tare after purge | Fixed | `1c3c6c7` | Also asks when the cup seems to be off |
| F-12 | `-Ofast` removes NaN checks | Fixed | `2ee7384` | |
| F-13 | Dry run not stopped | Fixed | `1c3c6c7` | "No beans?" after 5 s without 0.2 g |
| F-14 | Pulse completion polling | Fork, then Fixed | `2ee7384` | Completion interrupt; see N-B6 |
| F-15, F-16 | Pulse start and RMT reset races | Fork | | |
| F-17 | Settling on 0-2 samples | Fixed | `67dc45e` | Three samples minimum, drift limit |
| F-18 | Tare from an unsettled window | Fixed | `67dc45e` | |
| F-19 | Dropped UI state events | Fork | | |
| F-20 | Auto-tune cancel | Fork | | |
| F-21 | Auto-tune rounding and retests | Partly (fork) | | Tuning quality only; not changed |
| F-22 | No settling timeout | Fixed | `1c3c6c7` | 3 s around pulses, 5 s final |
| F-23 | Flash writes and blocking logs mid-grind | Fixed | `1c3c6c7`, `09b9e57` | Test-file writes, uptime save, layout save and the flow log moved out of motor phases |
| F-24 | Flash work in Bluetooth callbacks | Partly | `f3df959` | Refused while grinding; still runs in the Bluetooth task when idle |
| F-25 | Export stream closed from a Bluetooth callback without a lock | Open | | Export only, motor idle; needs locking in the Bluetooth task |
| F-26 | Old-session cleanup sort | Fork | | |
| F-27 | No firmware rollback | Fixed | `c9b4665` | Image confirmed after 20 s of healthy running |
| F-28 | UI stuck on the update screen | Fixed | `c9b4665` | |
| F-29 | Stored values not validated | Fixed | `1c3c6c7` | |
| F-30 | Calibration bit-bangs the HX711 from the UI task | Fixed | `67dc45e` | |
| F-31 | Sample buffer shared without synchronisation | Fixed | `67dc45e` | |
| F-32 | Touch reads not validated | Fixed | `09b9e57` | ACK checking stays off by design |
| F-33 | Motor pin undriven until display init | Fixed | `2ee7384` | Reset and bootloader still need the hardware pull-down |
| F-34 | UI can freeze with no recovery | Partly | `1c3c6c7` | LVGL asserts reboot. The UI task is not on the watchdog, because tare and calibration still block it for up to about 12 s (F-35) |
| F-35 | Blocking tare and calibration in the UI task | Open | | Needs those flows made asynchronous |
| F-36 | Time arithmetic not wrap-safe | Fixed | `67dc45e`, `1c3c6c7` | |
| F-37 | Timeout 60 s in code, 30 s in docs; large doses always time out | Fixed | `2ee7384`, `a13c928` | Docs fixed. Largest dose lowered from 1000 g to 40 g, which fits in 60 s at 1 g/s |
| F-38 | V1 display pipeline | Open | | Performance; needs profiling on the device |
| F-39 | Host upload tool status handling | Partly (fork) | `f3df959` | Failure statuses now end the upload early with a hint. The device restarts before it can report success, so the tool still treats a disconnect after the last command as success; check the build number with `grinder.py info` |
| F-40 | Bluetooth chunk size, checksum | Open | | Transfer speed and integrity, not safety |

## Community-fork findings

| ID | Finding | Status | Where | Notes |
|---|---|---|---|---|
| N-B1 | Diagnostic dump shows Wi-Fi passwords | Fixed | `f3df959` | |
| N-B2 | Cup-removal guard disabled on most grinds | Fixed | `1c3c6c7` | -10 g fallback threshold |
| N-B3 | Guard counts control cycles, not samples | Fixed | `1c3c6c7` | |
| N-B4 | Controller lock held across blocking I/O | Partly | `f3df959` | WebSocket replies sent after unlocking; history writes still run under the lock with the motor off |
| N-B5 | Transfers not gated on grinding | Fixed | `f3df959` | |
| N-B6 | Pulse polling logs every 20 ms | Fixed | `2ee7384` | |
| N-B7 | Starts during calibration or scale view | Fixed | `f3df959` | Remote starts only from the main screen |
| N-B8 | Manual mode one swipe away, single tap | Partly | `09b9e57` | Swipe and double-tap guards; no hold-to-run |
| N-B9 | Paused time grind never ends | Fixed | `1c3c6c7` | Ends after 5 min, may dim |
| N-B10 | V2 display pipeline limits | Open | | Performance |
| N-B11 | Update suspends a task holding a lock | Partly | `f3df959`, `1c3c6c7` | Data commands refused during updates, test-file writes removed; tasks are still suspended wherever they are |

## Touchscreen findings

| ID | Finding | Status | Where | Notes |
|---|---|---|---|---|
| U1 | Swipe from the round button starts a grind | Fixed | `09b9e57` | |
| U2 | Double tap on STOP or OK starts a grind | Fixed | `09b9e57` | 0.7 s re-arm; STOP stays instant |
| U3 | Update or export hides STOP | Fixed | | See F-02, F-10 |
| U4 | UI commands race the control loop | Fork | | |
| U5 | Dropped state events | Fork | | |
| U6 | Layout toggle above STOP writes flash | Fixed | `09b9e57` | Long press; saved after the grind |
| U7 | Completion screen shows the live reading | Fixed | `09b9e57` | |
| U8 | Lifting the cup in final settling is an error | Fixed | `1c3c6c7` | Completes with the settled weight |
| U9 | Motor test forces the menu | Fixed | `09b9e57` | |
| U10 | First tap on a dimmed screen presses a control | Fixed | `09b9e57` | |
| U11 | Tare and calibration failures silent | Fixed | `67dc45e` | |
| U12 | Start silently refused on a scale fault | Fixed | `09b9e57` | |
| U13 | Jog adds a step on release | Fixed | `09b9e57` | |
| U14 | One failed touch read becomes a click | Fixed | `09b9e57` | |

**Low-severity UI items:** fixed on `09b9e57`: factory reset now clears grind
history as described, "12.34g g", "Last grind >0h ago", "-0.0g", the menu
typo, weights of 100 g and more in the progress ring, and the gap between
paired buttons. The warning-icon and error-screen descriptions in the docs
were corrected instead of changing behaviour. Still open, all layout work that
needs checking on the screen: over-wide titles, the auto-tune console under
Cancel, the calibration noise text over its title, status icons over the close
button, and the purge checkbox size.

## Follow-up review of this branch

Two further reviews checked the commits above: one the motor, grind and scale
changes, one the network, Bluetooth and update changes. Their IDs start with
G and W so they do not clash with the review's.

| ID | Finding | Status | Where | Notes |
|---|---|---|---|---|
| G1 | The purge re-tare left kept grounds out of the dose | Fixed | `c40f5a2` | Re-tares only if the cup was lifted or its reading moved more than 0.5 g |
| G2 | Lifting the cup right after the purge ends the grind | Fixed | `c40f5a2` | Shows the purge prompt instead, when one follows |
| G3 | Removal threshold kept the first cup's weight after the re-tare | Fixed | `e35bd56` | Fixed -10 g threshold after a re-tare |
| G4 | The -10 g fallback is far more sensitive than the referenced rule | Open | | Needs bench measurement of hand forces (bench check 8) |
| G5 | Some motor-driver init failures leave the pin undriven | Fixed | `c40f5a2` | Driven LOW on every failure path |
| G6 | A NaN purge amount or freshness setting is not rejected | Fixed | `c40f5a2` | |
| G7 | CONTINUE with a stale scale hides the prompt | Fixed | `c40f5a2` | The prompt stays |
| G8 | Pulse settling waits up to 6 s per correction, not 3 s | Fixed | `c40f5a2` | 3 s from the motor stop |
| G9 | A moving scale never fails a tare | Fixed | `c40f5a2` | The notice also tells a moving scale from a silent one |
| G10 | Motor Test and Tune Pulses ignore the safety stop | Fixed | `c40f5a2` | "Motor stopped" notice |
| G11 | The purge prompt has no time limit | Fixed | `c40f5a2` | Ends after 5 minutes, like a paused time grind |
| G12 | The settling rule is worded more strictly than the code | Fixed | `c40f5a2` | Comment corrected |
| W1 | Bodies of refused requests are buffered before the check | Fixed | `fba0c68` | Refused as soon as the headers end (403, 413, 415), body discarded unread |
| W2 | Updates accepted while the new image awaits confirmation | Fixed | `fba0c68` | Web and Bluetooth refuse; confirmation waits if another image is selected to boot |
| W3 | The health check only confirms that tasks were created | Fixed | `fba0c68` | Per-task loop heartbeats. Touch reads are not checked: the driver notes that the controller NACKs when idle. USB recovery documented |
| W4 | The update permission is not tied to whoever granted it | Open | | Design decision: a code shown on the grinder would have to be sent by the web page and both Bluetooth tools |
| W5 | A prepared Wi-Fi update briefly shows "Update Failed" | Fixed | `fba0c68` | |
| W6 | A rollback is silent | Fixed | `fba0c68` | Record kept until confirmation; Wi-Fi updates record the image's hash |
| W7 | The stall check can abort a healthy Bluetooth update | Fixed | `fba0c68` | Wrap-safe signed comparison |
| W8 | A refused step uses up the permission or the upload token | Fixed | `fba0c68` | |
| W9 | Setup-network exemptions cover the whole API | Fixed | `fba0c68` | Any-host reads for pages and probes only; "null" origin for the setup API only |
| W10 | A failed confirmation is never retried | Fixed | `fba0c68` | Retried every 5 s |
| W11 | The web flasher ignores refusal and error statuses | Fixed | `fba0c68` | |
| W12 | The screensaver upload accepts repeated file parts | Fixed | `fba0c68` | One image per request |
| W13 | The busy check for transfers is not atomic with the transfer flag | Open | | A grind started in the few milliseconds before a transfer sets its flag overlaps flash I/O; needs transfers to take the operation interlock |

## Bench checks before first use

1. Scope the motor GPIO through power-up, reset, USB flashing and a panic
   reboot, with and without the recommended 10 kΩ pull-down. It should stay
   LOW until a grind starts.
2. Dead-man: in a debug build, stall the grind control task during a
   continuous run (for example with a long delay) and confirm the motor stops
   within about 1 s and starts are refused until restart.
3. Pulse lengths on a logic analyser, including the 1 s motor test.
4. Dry run: start a weight grind with an empty hopper; it should stop with
   "No beans?" about 5 s after priming starts.
5. Settling and tare times on your load cell; the stricter checks may add a
   few hundred milliseconds per pulse on a noisy setup.
6. Purge: keep the grounds and tap CONTINUE (no re-tare; weigh the cup: the
   dose includes the purge). After a restart, when the prompt is due again,
   lift the cup as the motor stops (the prompt appears at once), tip it out
   and tap CONTINUE with the cup off
   (prompt expected), tap BACK (the purge prompt returns with its STOP button),
   then CONTINUE with the cup back (re-tare, then grind). Lift the cup after
   the re-tare: the grind should stop.
7. Lift the cup mid-grind (stop with "Err: neg wt") and during final settling
   (completes with the result).
8. Touch: swipe across the round button, double-tap STOP and OK, and tap a
   dimmed screen. On a second grind after boot (the zero then includes the
   cup), steady the cup or portafilter handle as you normally would and note
   whether it stops the grind with "Err: neg wt" (G4).
9. A Wi-Fi and a Bluetooth update with and without **Allow Update**, then a
   deliberately bad image to confirm rollback and the "Update Failed" notice.
   Straight after an update, a second one should be refused for about
   20 seconds. Install one update with the load cell unplugged: the log should
   still report the new firmware as confirmed after 20 seconds.
