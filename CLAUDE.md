## Project Overview

ESP32-S3 intelligent coffee scale with grind-by-weight functionality. Features predictive grinding system, LVGL touch UI, and BLE OTA updates. Automatically grinds coffee beans to precise target weights using flow prediction and pulse correction algorithms.

## Essential Commands

All development tasks use the unified cross-platform Python tool:

```bash
# Build and upload
python3 tools/grinder.py build-upload

# Data analysis (exports data and launches Streamlit report)  
python3 tools/grinder.py analyze
```

**Common Commands:**
- `tools/venv/bin/python3 tools/grinder.py build --hardware v1 --jobs 8` - Build V1 firmware
- `tools/venv/bin/python3 tools/grinder.py build --hardware v2 --jobs 8` - Build V2 firmware
- `python3 tools/grinder.py upload` - Upload latest firmware via BLE (first tap Menu → Firmware → Allow Update on the grinder; the permission lasts 2 minutes)
- `python3 tools/grinder.py export` - Export grind data to database
- `python3 tools/grinder.py report` - Launch Streamlit report from existing data
- `python3 tools/grinder.py scan` - Scan for BLE devices
- `python3 tools/grinder.py info` - Get device system information
- `python3 tools/grinder.py clean` - Clean build artifacts

**Host tests (run after every change; no hardware needed):**
- `python3 -m unittest discover -s tools/tests -p '*_test.py'` - Firmware regression tests (compile real sources against stubs with the host g++)
- `node tools/tests/settings_web_test.mjs` - Embedded settings page workflow
- `node tools/tests/ota_web_test.mjs` - Embedded firmware-update page feedback
- `node tools/tests/web_flasher_status_test.mjs` - Web Bluetooth flasher update statuses

## Architecture

**4-Layer Architecture:**
1. **Hardware Layer** (`src/hardware/`): ESP32-S3 peripheral abstraction
2. **Control Layer** (`src/controllers/`): Business logic and algorithms  
3. **System Layer** (`src/system/`): State management
4. **UI Layer** (`src/ui/`): LVGL touchscreen interface

**Key Components:**
- **HardwareManager**: Central hardware coordinator
- **GrindController**: Multi-phase state machine with predictive flow control, 10 pulse corrections, mechanical instability detection, time mode additional pulses, and target-free manual grinding
- **LoadCell (HX711)**: Multi-mode precision weight measurement (instant, smoothed, filtered), calibration flag, noise diagnostics
- **DiagnosticsController**: System health monitoring (calibration status, sustained noise, mechanical instability), state persistence, hysteresis, priority-based warnings
- **UIManager**: LVGL screen management; ready screen is a swipe-only tabview (Manual, Single, Double, Custom, Wi-Fi, Menu; tab buttons hidden) with a non-clickable page-indicator dot row above the grind button, updated from `ReadyUIController::handle_tab_change` and `ReadyScreen::set_active_tab`; menu page surfaces quick Tools (Scale view, Calibrate, Pulse Tune, Motor Test, Firmware) followed by Settings (Bluetooth, Wi-Fi, Display, Grind Settings) and Info sections (Diagnostics, System Info, Logs & Data, Lifetime Stats), warning icon indicator, split-button layout for time mode pulses
- **StateMachine**: Central state coordination (READY → GRINDING → GRIND_COMPLETE)
- **Network security**: `RequestGuard` middleware (host allowlist plus same-origin check, `src/network/request_policy.h`) admits every web route and the WebSocket handshake; upload callbacks run before middleware, so upload routes call `RequestGuard::admit` themselves. Web and BLE firmware updates consume `update_authorization()`, granted at Menu → Firmware; remote grind starts need the on-device opt-in (`device_api.remote_start_enabled()`). Never make either permission settable over the network. See `docs/WIFI_ARCHITECTURE.md#security-model`.

**Update Intervals:** 20ms grind control, 20ms load cell polling (HX711 at 10 SPS), 16ms UI, 20ms Bluetooth, 100ms file I/O

**Grind Phases:**
- Standard phases: IDLE, INITIALIZING, SETUP, TARING, TARE_CONFIRM, PRIME, PRIME_SETTLING, PREDICTIVE, PULSE_DECISION, PULSE_EXECUTE, PULSE_SETTLING, FINAL_SETTLING, TIME_GRINDING, MANUAL_GRINDING, COMPLETED, TIMEOUT
- `TIME_ADDITIONAL_PULSE` - Dedicated phase for post-completion additional grinding pulses in time mode
- `PURGE_CONFIRM` - Pauses after chute operation (in Purge mode) to allow user to discard grinds before continuing to main grind
- `REFILL_CONFIRM` - Motor off: a weight grind ran out of beans and waits for the user to add beans and continue (see Out of Beans below)
- **Timeouts**: Targeted grinds stop after 60 seconds; target-free Manual mode has its own 30-second safety cutoff

**Grinder Purge/Prime:**
- **Always runs** before weight-mode grinding to saturate the grinder for accurate latency detection
- **Prime mode**: Keeps coffee, continues immediately to PREDICTIVE phase
- **Purge mode** (default): Shows confirmation popup, waits for user to discard stale grinds, then resumes in PREDICTIVE. CONTINUE keeps the pre-purge zero (kept grounds count as dose) unless the cup was lifted or its reading moved more than `GRIND_PURGE_RETARE_THRESHOLD_G`; then it re-tares (TARING → TARE_CONFIRM) first. Lifting the cup in PRIME_SETTLING shows the prompt at once; an unanswered prompt ends after `GRIND_PAUSE_MAX_MS`
- **Dry run**: PRIME or PREDICTIVE gaining under `GRIND_DRY_RUN_MIN_PROGRESS_G` in `GRIND_DRY_RUN_TIMEOUT_MS` pauses at the refill prompt (Out of Beans below); it stops with "No beans?" only when the refill limit is used up or the stretch since the last refill gained nothing
- **Configurable amount**: 0.1g-2.5g (default 1.0g)
- **Purge popup**: its checkbox switches the mode from Purge to Prime in preferences
- **Logging disabled** during PURGE_CONFIRM phase to avoid capturing data while paused
- **Preferences**: `grinder_mode` (int: 0=Prime, 1=Purge, default=1), `purge_amount_g` (float: 0.1-2.5, default=1.0). NVS keys must be 15 characters or fewer.

**Out of Beans (refill and resume):**
- **Trigger**: the dry-run rule in PRIME or PREDICTIVE stops the motor and enters `REFILL_CONFIRM` instead of ending the grind. Title "No beans" when nothing was ground yet (PRIME), otherwise "Out of beans"; the prompt shows the dose so far against the target
- **Buttons**: STOP (left) ends the grind as before ("No beans?", record kept in history); ✓ (right) continues. ✓ needs a deliberate tap (`USER_BUTTON_REARM_MS`) and is greyed while a press waits for the scale
- **CONTINUE compares settled readings only**: the reference is the settled reading after the motor stopped; on ✓ the controller uses `check_settling_complete` and, if the scale is moving, waits with the motor off for up to `GRIND_REFILL_SETTLE_TIMEOUT_MS`, then drops the press ("Scale not steady") without starting the motor. A cup lift while waiting also drops it
- **Zero kept**: grounds in the cup count toward the dose. A settled reading within `GRIND_REFILL_MOVED_THRESHOLD_G` of the reference continues, even after a lift; otherwise "Cup moved?" asks the user to put the cup back or continue from the current reading. With no cup on the scale (reading at or below the removal threshold), "Cup missing?" only asks for the cup back; nothing can continue without it
- **Resume**: PREDICTIVE (PRIME if it ran out in PRIME), no re-prime. Flow start and latency are measured again; the stop offset starts from the larger of `GRIND_UNDERSHOOT_TARGET_G` and the peak offset seen on healthy flow, and correction pulses start from the peak healthy flow (the running-dry taper reads low). If the remaining dose is already within the stop offset, it resumes straight into the correction pulses
- **Limits**: each resumed stretch has its own `GRIND_TIMEOUT_SEC`; pause time never counts. At most `GRIND_REFILL_MAX_RESUMES` resumes; a resumed stretch that gains nothing (jam, open relay, stuck reading) ends the grind. An unanswered prompt ends after `GRIND_PAUSE_MAX_MS` as "No beans?"
- **Not covered yet**: running out during correction pulses still ends "COMPLETE - MAX PULSES" (planned milestone 3)
- **Remote**: the web API reports the prompt as `PAUSED` and can STOP it; continuing is on-device only
- **Data**: logging pauses at the prompt; the session record stores `refill_count` (formerly a reserved byte, so older files read 0); events after a resume carry `GRIND_EVENT_FLAG_AFTER_REFILL`; resumed grinds are left out of the lifetime accuracy and pulse averages

**Time Mode Pulses:** Split-button completion screen (OK + PULSE), `TIME_ADDITIONAL_PULSE` phase, 100ms duration

**Grind Settings:** Configurable through Menu → Grind Settings page
- **Mode**: Radio buttons for Weight/Time mode selection
- **Swipe Gestures Toggle**: Enable/disable vertical swipe gestures for mode switching (default: disabled)
- **Automation**: Start on Cup and Return on Removal toggles
- **Purging**: Radio buttons (Prime/Purge) and Amount slider (0.1g-2.5g)
- **Preferences**: `swipe.enabled` (boolean), `grind_mode` (0=Weight, 1=Time), `grinder_mode` (0=Prime, 1=Purge), `purge_amount_g` (float)
- **Behavior**: Swipe gestures only work when enabled; direct mode selection always works

**Color Scheme (24-bit hex, passed to `lv_color_hex`; see `src/config/theme.h`):**
- `COLOR_PRIMARY`: 0xFF3D00 (Red) - Primary theme color
- `COLOR_ACCENT`: 0x00AAFF (Blue) - Highlights and accents
- `COLOR_SUCCESS`: 0x00AA00 (Green) - Success states
- `COLOR_WARNING`: 0xCC8800 (Orange) - Warning states
- `COLOR_BACKGROUND`: 0x000000 (Black) - Background
- `COLOR_TEXT_PRIMARY`: 0xFFFFFF (White) - Primary text

**Font Usage Hierarchy:**
- `lv_font_montserrat_24`: Standard text and button labels
- `lv_font_montserrat_32`: Button symbols (OK, CLOSE, PLUS, MINUS)
- `lv_font_montserrat_36`: Screen titles
- `lv_font_montserrat_56`: Large weight displays

## Development Notes

* When modifying this codebase, follow the existing architectural patterns, maintain the clean separation between layers, and ensure any timing-critical code respects the established update intervals.
* after making a test build let me know the build number
* Always read entire files. Otherwise, you don’t know what you don’t know, and will end up making mistakes, duplicating code that already exists, or misunderstanding the architecture.  
* Commit early and often. When working on large tasks, your task could be broken down into multiple logical milestones. After a certain milestone is completed and confirmed to be ok by the user, you should commit it. If you do not, if something goes wrong in further steps, we would need to end up throwing away all the code, which is expensive and time consuming.  
* Your internal knowledgebase of libraries might not be up to date. When working with any external library, unless you are 100% sure that the library has a super stable interface, you will look up the latest syntax and usage via either Perplexity (first preference) or web search (less preferred, only use if Perplexity is not available)  
* Do not say things like: “x library isn’t working so I will skip it”. Generally, it isn’t working because you are using the incorrect syntax or patterns. This applies doubly when the user has explicitly asked you to use a specific library, if the user wanted to use another library they wouldn’t have asked you to use a specific one in the first place.  
* Always run linting after making major changes. Otherwise, you won’t know if you’ve corrupted a file or made syntax errors, or are using the wrong methods, or using methods in the wrong way.   
* Please organise code into separate files wherever appropriate, and follow general coding best practices about variable naming, modularity, function complexity, file sizes, commenting, etc.  
* Code is read more often than it is written, make sure your code is always optimised for readability  
* Unless explicitly asked otherwise, the user never wants you to do a “dummy” implementation of any given task. Never do an implementation where you tell the user: “This is how it *would* look like”. Just implement the thing.  
* Whenever you are starting a new task, it is of utmost importance that you have clarity about the task. You should ask the user follow up questions if you do not, rather than making incorrect assumptions.  
* Do not carry out large refactors unless explicitly instructed to do so.  
* When starting on a new task, you should first understand the current architecture, identify the files you will need to modify, and come up with a Plan. In the Plan, you will think through architectural aspects related to the changes you will be making, consider edge cases, and identify the best approach for the given task. Get your Plan approved by the user before writing a single line of code.   
* If you are running into repeated issues with a given task, figure out the root cause instead of throwing random things at the wall and seeing what sticks, or throwing in the towel by saying “I’ll just use another library / do a dummy implementation”.   
* You are an incredibly talented and experienced polyglot with decades of experience in diverse areas such as software architecture, system design, development, UI & UX, copywriting, and more.  
* When doing UI & UX work, make sure your designs are both aesthetically pleasing, easy to use, and follow UI / UX best practices. You pay attention to interaction patterns, micro-interactions, and are proactive about creating smooth, engaging user interfaces that delight users.   
* When you receive a task that is very large in scope or too vague, you will first try to break it down into smaller subtasks. If that feels difficult or still leaves you with too many open questions, push back to the user and ask them to consider breaking down the task for you, or guide them through that process. This is important because the larger the task, the more likely it is that things go wrong, wasting time and energy for everyone involved.
- Touch polling now uses the IDF I2C master driver with ACK checking disabled so idle NACKs don't spam logs. Toggle `DEBUG_SUPPRESS_TOUCH_I2C_ERRORS` to 0 if you need the raw driver output for troubleshooting.
- Use the src/config/constants.h aggregation file to include constants / settings - dont refer to config files directly.
- When new features have been added and tested always update the docs as well
- when making a commit, only focus on the end result not the process we went through to get to the end result
