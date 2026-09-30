# sim/qa: visual QA suite for the digital-twin page

Drives `sim/dist/index.html` (opened only via `file://`) in headless Chromium with `playwright-core` (no browser
download: it uses the preinstalled `/opt/pw-browsers/chromium-1194/chrome-linux/chrome`, override with `CHROME_PATH`).

```
PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm --prefix sim/qa install     # once
node sim/qa/run_qa.mjs                                             # or: npm --prefix sim/qa test
node sim/qa/run_qa.mjs --only s1,s6 --vp 1280x800                  # subset while developing (does not write REPORT.md)
```

Exit code 0 when no blocker/major defect is found (automated checks plus `manual_defects.json`); about 2.5 minutes.
Outputs: `REPORT.md`, `out/` (every screenshot, downloaded CSV/log files, `values.json`; gitignored),
`screenshots/` (a few scaled representative PNGs, committed).

| File | Purpose |
|------|---------|
| `run_qa.mjs` | entry point: two viewports side by side (1280x800, 1920x1080), then the speed test alone, plus one 1024x768 check |
| `scenarios.mjs` | scenarios 1 (normal 18.0 g), 2 (run-dry), 3 (cup removed), 5 (manual taps), 7 (fault buttons), 8 (exports) |
| `extra.mjs` | layout checks, narrow check, 20x speed measurement, 3D view checks (skipped if the card is absent) |
| `lib.mjs` | launch, per-scenario session with console/request capture, waiting helpers, PNG analysis in a scratch page |
| `report.mjs` | writes `REPORT.md` |
| `manual_defects.json`, `visual_review.md` | defects and screenshot descriptions from looking at the PNGs (hand written, included in `REPORT.md`) |

Method notes:
- Each scenario opens a fresh page (new WASM instance) and restarts into a fixed-seed world (seed 1) held paused at power-on
  (t = 6.5 s), so scripted runs start at identical virtual time; results do not depend on wall-clock speed.
- Result screens last 3 s of virtual time before the scripted operator dismisses them, so an in-page watcher pauses the
  page on the first animation frame after COMPLETED/TIMEOUT; pixel samples and screenshots are taken while paused.
- Headless Chromium runs with `--disable-gpu` (WebGL then falls back to software rendering); forcing SwiftShader made
  the page about 5x slower in this VM.
- `out/values.json` holds the values that must be identical between runs (determinism check: run twice and compare).
