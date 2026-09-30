#!/usr/bin/env python3
"""Monte Carlo harness for the grind-by-weight digital twin.

    python3 sim/mc/run_mc.py [--quick] [--jobs 4]
    python3 sim/mc/run_mc.py --emit-params sweep 100007      # print the sampled plant params of one run

Runs every scenario set through `sim/out/host/grindsim`, writes raw CSVs to sim/mc/out/<mode>/,
and writes sim/reports/montecarlo.md plus SVG charts (quick mode writes to sim/reports/quick/).
Standard library only (charts are hand-written SVG). Seeds are fixed below, and the report contains
no timestamps or wall times, so a rerun with the same binary gives an identical report.
"""
import argparse
import csv
import hashlib
import io
import json
import math
import os
import random
import re
import subprocess
import sys
import time
from collections import Counter, OrderedDict, defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
MC_DIR = ROOT / "sim" / "mc"
SCEN_DIR = MC_DIR / "scenarios"
REPORT_DIR_FULL = ROOT / "sim" / "reports"
GRINDSIM = ROOT / "sim" / "out" / "host" / "grindsim"
PARAMS_DEFAULT = ROOT / "sim" / "plant" / "params_default.json"

# ---------------------------------------------------------------- report-level definitions
TOL_G = 0.03              # GRIND_ACCURACY_TOLERANCE_G
SIGNAL_LIMIT_S = 0.5      # has_recent_sample window
TIME_LIMIT_S = 60.0       # GRIND_TIMEOUT_SEC
AFTER_END_LIMIT_S = 1.0   # report's own definition (motor on after firmware ended the session)
GRINDSIM_REL = "sim/out/host/grindsim"

# Seeds are fixed here. Each set runs seeds base .. base+N-1 (full N below, quick N in SIZES).
SEED_BASE = {
    "nominal": 1000, "sweep": 100000, "sweep_gain": 200000, "purge_keep": 300000, "prime": 400000,
    "rundry": 500000, "faults": 600000, "sps80": 700000, "sps30": 710000, "targets": 800000,
}
SIZES = {  # (full, quick) number of seeds per scenario file
    "nominal": (500, 24), "sweep": (1000, 40), "sweep_gain": (300, 12), "purge_keep": (300, 16),
    "prime": (300, 16), "rundry": (30, 3), "faults": (20, 2), "sps80": (300, 16), "sps30": (300, 16), "targets": (60, 4),
}
SWEEP_HOLD_DEFAULT = ["hx711_sps", "lc_counts_per_g", "lc_baseline_code", "hx711_settle_ms_80sps"]

# Categorical colours (light surface), fixed order.
C_BLUE, C_ORANGE, C_AQUA, C_YELLOW, C_MAGENTA, C_GREEN, C_VIOLET, C_RED = (
    "#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948")
C_GREY = "#b5b4ad"
C_SLATE = "#52514e"
INK, INK2, GRID, SURFACE, BAND = "#0b0b0b", "#52514e", "#e3e2dc", "#ffffff", "#e6eef9"

OUTCOME_CATS = OrderedDict([
    ("SUCCESS in band", C_BLUE),
    ("SUCCESS overshoot (true err > +0.03 g)", C_ORANGE),
    ("SUCCESS undershoot (true err < -0.03 g)", C_AQUA),
    ("OVERSHOOT", C_MAGENTA),
    ("MAX_PULSES", C_YELLOW),
    ("TIMEOUT", C_VIOLET),
    ("ERROR", C_RED),
    ("SCALE_ERROR", C_SLATE),
    ("NONE / other", C_GREY),
])


# ================================================================== scenario catalogue
def fault_events(kind, when, delay):
    """Event list for one fault family injected at when+delay."""
    def f(name, active=1, value=None, d=delay):
        e = {"when": when, "delay_s": round(d, 3), "fault": name, "active": active}
        if value is not None:
            e["value"] = value
        return e
    def a(name, value=None):
        e = {"when": when, "delay_s": round(delay, 3), "action": name}
        if value is not None:
            e["value"] = value
        return e
    if kind == "lc_disconnect": return [f("lc_disconnect")]
    if kind == "lc_stuck": return [f("lc_stuck")]
    if kind == "lc_noise_burst_1s": return [f("lc_noise_burst", 1, 20), f("lc_noise_burst", 0, None, delay + 1.0)]
    if kind == "lc_noise_burst_perm": return [f("lc_noise_burst", 1, 20)]
    if kind == "relay_stuck_on": return [f("relay_stuck_on")]
    if kind == "relay_stuck_off": return [f("relay_stuck_off")]
    if kind == "remove_cup": return [a("remove_cup")]
    if kind == "bump30": return [a("bump", 30)]
    if kind == "bump200": return [a("bump", 200)]
    if kind == "motor_stall": return [f("motor_stall")]
    if kind == "motor_stall_2s": return [f("motor_stall"), f("motor_stall", 0, None, delay + 2.0)]
    if kind == "feed_block": return [f("feed_block")]
    if kind == "reset": return [a("reset")]
    raise ValueError(kind)


def build_catalogue():
    """Returns list of scenario-set dicts. Each scenario: file stem, json dict, extra grindsim args."""
    sets = []
    base = {"target_g": 18.0, "beans_g": 22.0, "purge_action": "discard"}

    def sc(stem, **kw):
        d = {"name": stem}
        d.update(base)
        d.update(kw)
        return {"stem": stem, "json": d, "extra": [], "meta": {}}

    sets.append(dict(key="nominal", title="Nominal", mode="batch", params=None, seeds="nominal",
                     desc="Target 18.0 g, 22.0 g of beans loaded (enough for the 1.0 g purge), purge prompt answered by "
                          "discarding the purge, default plant parameters; only the seed (noise, clumps) varies.",
                     scenarios=[sc("nominal")]))
    sets.append(dict(key="sweep", title="Parameter sweep", mode="single", params="sweep", seeds="sweep",
                     desc="As nominal, but every plant parameter except hx711_sps, lc_counts_per_g, lc_baseline_code "
                          "and hx711_settle_ms_80sps is drawn uniformly from its sweep range (params_default.json); "
                          "lc_gain_error is held at 0 (see method).",
                     scenarios=[sc("sweep")]))
    sets.append(dict(key="sweep_gain", title="Parameter sweep incl. gain error", mode="single", params="sweep_gain",
                     seeds="sweep_gain",
                     desc="As the parameter sweep, but lc_gain_error is also sampled (-0.05 .. +0.05), i.e. the plant "
                          "load cell sensitivity differs from the firmware calibration.",
                     scenarios=[sc("sweep_gain")]))
    sets.append(dict(key="purge_keep", title="Purge: keep", mode="batch", params=None, seeds="purge_keep",
                     desc="Purge mode (default) but the user presses CONTINUE without touching the cup "
                          "(kept grounds count as dose).",
                     scenarios=[sc("purge_keep", purge_action="keep")]))
    sets.append(dict(key="prime", title="Prime mode", mode="batch", params=None, seeds="prime",
                     desc="grinder_mode 0 (Prime): no purge prompt, purge grounds stay in the cup.",
                     scenarios=[sc("prime", purge_mode=0)]))
    sets.append(dict(key="sps80", title="HX711 80 SPS", mode="batch", params=None, seeds="sps80",
                     desc="As nominal with --set hx711_sps=80.", scenarios=[dict(
                         sc("sps80"), extra=["--set", "hx711_sps=80"])]))
    sets.append(dict(key="sps30", title="HX711 30 SPS (model-only extra)", mode="batch", params=None, seeds="sps30",
                     desc="As nominal with --set hx711_sps=30. Extra to the task list: 30 SPS is below the firmware's 40 SPS "
                          "validation limit so the scale boots; it is not a real HX711 rate (10 and 80 SPS), it shows the "
                          "effect of a faster conversion cadence.",
                     scenarios=[dict(sc("sps30"), extra=["--set", "hx711_sps=30"])]))
    # targets
    tg = []
    for t in (9, 12, 15, 18, 21, 25, 30, 40):
        s = sc("target_%02d" % t, target_g=float(t), beans_g=float(t + 4))
        s["meta"] = {"target": t}
        tg.append(s)
    sets.append(dict(key="targets", title="Targets", mode="batch", params=None, seeds="targets",
                     desc="Targets 9, 12, 15, 18, 21, 25, 30, 40 g with beans = target + 4 g; default plant.",
                     scenarios=tg))
    # run-dry
    rd = []
    for mode_key, kw in (("discard", dict(purge_mode=1, purge_action="discard")),
                         ("keep", dict(purge_mode=1, purge_action="keep")),
                         ("prime", dict(purge_mode=0, purge_action="discard"))):
        for b in (17.0, 17.5, 18.0, 18.5, 19.0, 19.5, 20.0, 20.5, 21.0, 22.0):
            s = sc("rundry_%s_b%.1f" % (mode_key, b), beans_g=b, **kw)
            s["meta"] = {"beans": b, "purge": mode_key}
            rd.append(s)
    sets.append(dict(key="rundry", title="Single-dose run-dry", mode="batch", params=None, seeds="rundry",
                     desc="Beans loaded 17.0 .. 22.0 g (10 values) x {purge discard, purge keep, prime}; target 18.0 g; "
                          "default plant. Low values end because the burrs run empty, not because the controller stopped.",
                     scenarios=rd))
    # faults
    rng = random.Random("mc-fault-delays-v1")
    rnd = sorted(round(rng.uniform(0.5, 22.0), 1) for _ in range(4))
    timings = [("pred0_5", "phase:PREDICTIVE", 0.5), ("pred2", "phase:PREDICTIVE", 2.0),
               ("pred5", "phase:PREDICTIVE", 5.0), ("pdec0", "phase:PULSE_DECISION", 0.0)]
    timings += [("rnd%d" % (i + 1), "start", d) for i, d in enumerate(rnd)]
    families = [
        ("lc_disconnect", "Load cell disconnect", None),
        ("lc_stuck", "Load cell stuck reading", None),
        ("lc_noise_burst_1s", "Load cell noise burst x20, 1 s", None),
        ("lc_noise_burst_perm", "Load cell noise x20, permanent", ["pred2"]),
        ("relay_stuck_on", "Relay stuck on", "with_start"),
        ("relay_stuck_off", "Relay stuck off", "with_start"),
        ("remove_cup", "Cup removed (not replaced)", None),
        ("bump30", "Cup bumped, peak 30 g", None),
        ("bump200", "Cup bumped, peak 200 g", None),
        ("motor_stall", "Motor stall, permanent", None),
        ("motor_stall_2s", "Motor stall, 2 s", ["pred2", "pred5", "pdec0"]),
        ("feed_block", "Feed blocked (bridging)", None),
        ("reset", "Reset mid-grind (power-on reset)", None),
    ]
    fl = []
    for kind, label, only in families:
        tl = list(timings)
        if only == "with_start":
            tl = [("start0", "start", 0.0)] + tl
        elif only:
            tl = [t for t in timings if t[0] in only]
        if kind == "feed_block":
            tl = [("boot0", "boot", 0.0)] + tl
        for tname, when, delay in tl:
            s = sc("fault_%s_%s" % (kind, tname))
            s["json"]["events"] = fault_events(kind, when, delay)
            s["meta"] = {"family": kind, "label": label, "timing": tname, "when": when, "delay": delay}
            fl.append(s)
    s = sc("fault_beans10", beans_g=10.0)
    s["meta"] = {"family": "beans10", "label": "Beans run out early (10 g loaded, no event)", "timing": "-",
                 "when": "-", "delay": 0}
    fl.append(s)
    sets.append(dict(key="faults", title="Fault injection", mode="single", params=None, seeds="faults",
                     desc="Faults and user actions injected at PREDICTIVE+0.5/2/5 s, PULSE_DECISION+0 s and four fixed "
                          "random delays after START (%s s); default plant, purge discard."
                          % ", ".join("%.1f" % d for d in rnd),
                     scenarios=fl, logs=True))
    return sets


def write_scenarios(sets):
    SCEN_DIR.mkdir(parents=True, exist_ok=True)
    wanted = set()
    for st in sets:
        for s in st["scenarios"]:
            text = json.dumps(s["json"], indent=2) + "\n"
            p = SCEN_DIR / (s["stem"] + ".json")
            wanted.add(p.name)
            if not p.exists() or p.read_text() != text:
                p.write_text(text)
            s["path"] = p
            s["rel"] = "sim/mc/scenarios/" + p.name
    for p in SCEN_DIR.glob("*.json"):  # drop scenario files that are no longer in the catalogue
        if p.name not in wanted:
            p.unlink()


# ================================================================== plant parameter sampling
def load_defaults():
    with open(PARAMS_DEFAULT) as fh:
        return json.load(fh)


def sample_params(set_key, seed, defaults):
    rng = random.Random("mc-params-v1-%d" % seed)  # independent of the set: same seed, same draws
    out = OrderedDict()
    for name in sorted(defaults):
        d = defaults[name]
        if name in SWEEP_HOLD_DEFAULT:
            out[name] = d["value"]
            continue
        u = rng.random()  # always drawn so that streams line up between sweep and sweep_gain
        v = d["min"] + (d["max"] - d["min"]) * u
        out[name] = float("%.6g" % v)
    if set_key == "sweep":
        out["lc_gain_error"] = 0.0
    return out


# ================================================================== running grindsim
def fnum(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return float("nan")


def crash_row(seed, scen, msg):
    return {"seed": str(seed), "scenario": scen, "grind": "-1", "target_g": "nan", "loaded_g": "0",
            "terminal": "NONE", "result": "CRASH", "error": msg, "operator_status": "crash"}


def run_single(scen, seed, log_path=None, params_path=None, timeout=600):
    cmd = [str(GRINDSIM), "--seed", str(seed), "--scenario", str(scen["path"])]
    if params_path:
        cmd += ["--params", str(params_path)]
    cmd += scen["extra"]
    if log_path:
        cmd += ["--log", str(log_path)]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return [crash_row(seed, scen["stem"], "wall timeout")]
    rows = list(csv.DictReader(io.StringIO(p.stdout)))
    if p.returncode != 0 or not rows:
        return [crash_row(seed, scen["stem"], "exit %d %s" % (p.returncode, p.stderr.strip()[:80]))]
    return rows


def run_batch(scen, seed0, n, jobs, out_csv):
    cmd = [str(GRINDSIM), "--batch", str(n), "--seed", str(seed0), "--jobs", str(jobs),
           "--scenario", str(scen["path"])] + scen["extra"] + ["--summary", str(out_csv)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit("grindsim batch failed for %s: %s" % (scen["stem"], p.stderr))
    with open(out_csv, newline="") as fh:
        return list(csv.DictReader(fh))


def count_events(log_path):
    try:
        with open(log_path, errors="replace") as fh:
            return sum(1 for line in fh if "] event " in line)
    except OSError:
        return -1


def execute(sets, quick, jobs, outdir, log):
    """Run all sets. Returns dict set_key -> list of row dicts (extended with bookkeeping columns)."""
    defaults = load_defaults()
    raw = outdir / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    (outdir / "logs").mkdir(exist_ok=True)
    results = OrderedDict()
    timing = OrderedDict()
    sweep_rows = []
    for st in sets:
        t0 = time.time()
        n = SIZES[st["seeds"]][1 if quick else 0]
        seed0 = SEED_BASE[st["seeds"]]
        rows_out = []
        if st["mode"] == "batch":
            for s in st["scenarios"]:
                rows = run_batch(s, seed0, n, jobs, raw / (s["stem"] + ".csv"))
                for r in rows:
                    r["_file"] = s["stem"]
                    r["_events_total"] = 0
                    r["_events_fired"] = 0
                rows_out += rows
        else:
            tasks = []
            for s in st["scenarios"]:
                for k in range(n):
                    tasks.append((s, seed0 + k))
            pdir = outdir / "params" / st["key"]
            if st["params"]:
                pdir.mkdir(parents=True, exist_ok=True)

            def work(task):
                s, seed = task
                params_path = None
                if st["params"]:
                    params = sample_params(st["params"], seed, defaults)
                    params_path = pdir / ("%d.json" % seed)
                    params_path.write_text(json.dumps(params, indent=1, sort_keys=True) + "\n")
                log_path = outdir / "logs" / ("%s_%d.log" % (s["stem"], seed)) if st.get("logs") else None
                rows = run_single(s, seed, log_path, params_path)
                ev_total = len(s["json"].get("events", []))
                ev_fired = count_events(log_path) if log_path else 0
                for r in rows:
                    r["_file"] = s["stem"]
                    r["_events_total"] = ev_total
                    r["_events_fired"] = ev_fired
                return rows

            with ThreadPoolExecutor(max_workers=jobs) as ex:
                for rows in ex.map(work, tasks):
                    rows_out += rows
            if st["params"]:
                for k in range(n):
                    seed = seed0 + k
                    row = OrderedDict(seed=seed)
                    row.update(sample_params(st["params"], seed, defaults))
                    sweep_rows.append((st["key"], row))
        results[st["key"]] = rows_out
        timing[st["key"]] = (len(rows_out), time.time() - t0)
        log("  %-12s %5d rows  %6.1f s" % (st["key"], len(rows_out), time.time() - t0))
        # raw CSV per set
        if rows_out:
            cols = [c for c in rows_out[0].keys()]
            with open(raw / ("set_%s.csv" % st["key"]), "w", newline="") as fh:
                w = csv.DictWriter(fh, fieldnames=cols, extrasaction="ignore")
                w.writeheader()
                for r in rows_out:
                    w.writerow(r)
    return results, timing, sweep_rows


def load_previous(sets, quick, outdir):
    results, timing, sweep_rows = OrderedDict(), OrderedDict(), []
    defaults = load_defaults()
    for st in sets:
        with open(outdir / "raw" / ("set_%s.csv" % st["key"]), newline="") as fh:
            rows = list(csv.DictReader(fh))
        for r in rows:
            r["_events_total"] = int(r["_events_total"])
            r["_events_fired"] = int(r["_events_fired"])
        results[st["key"]] = rows
        timing[st["key"]] = (len(rows), 0.0)
        if st["params"]:
            n = SIZES[st["seeds"]][1 if quick else 0]
            for k in range(n):
                row = OrderedDict(seed=SEED_BASE[st["seeds"]] + k)
                row.update(sample_params(st["params"], row["seed"], defaults))
                sweep_rows.append((st["key"], row))
    return results, timing, sweep_rows


# ================================================================== statistics
def pct(vals, q):
    v = sorted(x for x in vals if not math.isnan(x))
    if not v:
        return float("nan")
    k = (len(v) - 1) * q
    lo, hi = int(math.floor(k)), int(math.ceil(k))
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def mean(v):
    v = [x for x in v if not math.isnan(x)]
    return sum(v) / len(v) if v else float("nan")


def sd(v):
    v = [x for x in v if not math.isnan(x)]
    if len(v) < 2:
        return float("nan")
    m = sum(v) / len(v)
    return math.sqrt(sum((x - m) ** 2 for x in v) / (len(v) - 1))


def ranks(v):
    order = sorted(range(len(v)), key=lambda i: v[i])
    r = [0.0] * len(v)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and v[order[j + 1]] == v[order[i]]:
            j += 1
        for k in range(i, j + 1):
            r[order[k]] = (i + j) / 2.0 + 1
        i = j + 1
    return r


def spearman(x, y):
    if len(x) < 3:
        return float("nan")
    rx, ry = ranks(x), ranks(y)
    mx, my = sum(rx) / len(rx), sum(ry) / len(ry)
    num = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    den = math.sqrt(sum((a - mx) ** 2 for a in rx) * sum((b - my) ** 2 for b in ry))
    return num / den if den else float("nan")


def f3(x, nd=3, signed=False):
    if x is None or (isinstance(x, float) and math.isnan(x)):
        return "n/a"
    return ("%+.*f" if signed else "%.*f") % (nd, x)


def ranges(seeds):
    """Compress a sorted list of ints into 'a-b, c' (every seed is included)."""
    seeds = sorted(set(seeds))
    out, i = [], 0
    while i < len(seeds):
        j = i
        while j + 1 < len(seeds) and seeds[j + 1] == seeds[j] + 1:
            j += 1
        out.append(str(seeds[i]) if i == j else "%d-%d" % (seeds[i], seeds[j]))
        i = j + 1
    return ", ".join(out)


# ================================================================== row classification
def primary_rows(rows):
    """One row per (file, seed): the first grind row (grind < 1000)."""
    seen = {}
    for r in rows:
        if int(fnum(r.get("grind", -1))) >= 1000:
            continue
        key = (r["_file"], r["seed"])
        if key not in seen:
            seen[key] = r
    return list(seen.values())


def tags_of(r):
    t = []
    if fnum(r.get("motor_invalid_signal_s")) > SIGNAL_LIMIT_S or fnum(r.get("motor_no_sample_s")) > SIGNAL_LIMIT_S:
        t.append("SAFETY-SIGNAL")
    if fnum(r.get("motor_max_run_s")) > TIME_LIMIT_S or fnum(r.get("motor_after_end_s")) > AFTER_END_LIMIT_S:
        t.append("SAFETY-TIME")
    return t


def outcome_cat(r):
    res = r.get("result", "NONE")
    if res == "SUCCESS":
        e = fnum(r["err_true_g"])
        if math.isnan(e):
            return "NONE / other"
        if e > TOL_G + 1e-9:
            return "SUCCESS overshoot (true err > +0.03 g)"
        if e < -TOL_G - 1e-9:
            return "SUCCESS undershoot (true err < -0.03 g)"
        return "SUCCESS in band"
    if res in ("OVERSHOOT", "MAX_PULSES", "TIMEOUT", "ERROR", "SCALE_ERROR"):
        return res
    return "NONE / other"


def outcome_label(r):
    """Finer outcome label used for grouping: result + firmware error text."""
    res = r.get("result", "NONE")
    if res == "SUCCESS":
        c = outcome_cat(r)
        return {"SUCCESS in band": "SUCCESS (in band)",
                "SUCCESS overshoot (true err > +0.03 g)": "SUCCESS (true err > +0.03)",
                "SUCCESS undershoot (true err < -0.03 g)": "SUCCESS (true err < -0.03)"}.get(c, "SUCCESS")
    if res == "NONE":
        return "NONE (no result: %s)" % (r.get("operator_status") or "").strip() if r.get("grind") in ("-1", "0") else "NONE"
    err = (r.get("error") or "").strip()
    return "%s '%s'" % (res, err) if err else res


def failure_signature(r, post_reset=False):
    """Returns (signature, tags) or None if the row is not a failure."""
    tags = tags_of(r)
    if post_reset:
        return ("post-reset observation", tags) if tags else None
    lab = outcome_label(r)
    failed = not lab.startswith("SUCCESS (in band)") and lab != "SUCCESS"
    if not failed and not tags:
        return None
    sig = lab if failed else lab
    return (sig, tags)


def repro_command(st, scen, seed, outdir_rel):
    c = "%s --seed %d --scenario %s" % (GRINDSIM_REL, seed, scen["rel"])
    if st["params"]:
        c += " --params %s/params/%s/%d.json" % (outdir_rel, st["key"], seed)
    for a in scen["extra"]:
        c += " " + a
    return c


# ================================================================== SVG toolkit
class Svg:
    def __init__(self, w, h, title, desc=""):
        self.w, self.h = w, h
        self.parts = []
        self.title, self.desc = title, desc

    def add(self, s):
        self.parts.append(s)

    def text(self, x, y, s, size=11, anchor="start", fill=INK2, weight="normal", rotate=None):
        tr = ' transform="rotate(%s %.1f %.1f)"' % (rotate, x, y) if rotate else ""
        self.add('<text x="%.1f" y="%.1f" font-size="%s" text-anchor="%s" fill="%s" font-weight="%s"%s>%s</text>'
                 % (x, y, size, anchor, fill, weight, tr, escape(str(s))))

    def line(self, x1, y1, x2, y2, stroke=GRID, width=1, dash=None):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        self.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%s"%s/>'
                 % (x1, y1, x2, y2, stroke, width, d))

    def rect(self, x, y, w, h, fill, opacity=None, stroke=None, title=None, rx=0):
        o = ' fill-opacity="%s"' % opacity if opacity is not None else ""
        s = ' stroke="%s" stroke-width="1"' % stroke if stroke else ""
        r = ' rx="%s"' % rx if rx else ""
        inner = "<title>%s</title>" % escape(title) if title else ""
        self.add('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" fill="%s"%s%s%s>%s</rect>'
                 % (x, y, max(w, 0), max(h, 0), fill, o, s, r, inner))

    def marker(self, shape, cx, cy, r, color):
        if shape == "circle":
            self.add('<circle cx="%.1f" cy="%.1f" r="%s" fill="%s" stroke="%s" stroke-width="1"/>' % (cx, cy, r, color, SURFACE))
        elif shape == "square":
            self.add('<rect x="%.1f" y="%.1f" width="%s" height="%s" fill="%s" stroke="%s" stroke-width="1"/>'
                     % (cx - r, cy - r, 2 * r, 2 * r, color, SURFACE))
        elif shape == "diamond":
            self.add('<path d="M%.1f %.1fL%.1f %.1fL%.1f %.1fL%.1f %.1fZ" fill="%s" stroke="%s" stroke-width="1"/>'
                     % (cx, cy - r - 1, cx + r + 1, cy, cx, cy + r + 1, cx - r - 1, cy, color, SURFACE))
        elif shape == "triangle":
            self.add('<path d="M%.1f %.1fL%.1f %.1fL%.1f %.1fZ" fill="%s" stroke="%s" stroke-width="1"/>'
                     % (cx, cy - r - 1, cx + r + 1, cy + r, cx - r - 1, cy + r, color, SURFACE))
        elif shape == "cross":
            self.add('<path d="M%.1f %.1fL%.1f %.1fM%.1f %.1fL%.1f %.1f" stroke="%s" stroke-width="2" fill="none"/>'
                     % (cx - r, cy - r, cx + r, cy + r, cx - r, cy + r, cx + r, cy - r, color))
        else:  # plus
            self.add('<path d="M%.1f %.1fL%.1f %.1fM%.1f %.1fL%.1f %.1f" stroke="%s" stroke-width="2" fill="none"/>'
                     % (cx - r - 1, cy, cx + r + 1, cy, cx, cy - r - 1, cx, cy + r + 1, color))

    def save(self, path):
        head = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
                'font-family="Helvetica, Arial, sans-serif" role="img">' % (self.w, self.h, self.w, self.h))
        body = "<title>%s</title><desc>%s</desc>" % (escape(self.title), escape(self.desc))
        bg = '<rect width="%d" height="%d" fill="%s"/>' % (self.w, self.h, SURFACE)
        Path(path).write_text(head + body + bg + "\n".join(self.parts) + "</svg>\n")


def nice_ceiling(v):
    if v <= 0:
        return 1.0
    e = 10 ** math.floor(math.log10(v))
    for m in (1, 2, 2.5, 5, 10):
        if m * e >= v:
            return m * e
    return 10 * e


def legend(svg, x, y, items, gap=16):
    """items: (label, color, shape). Shape 'box' | 'line' | marker names."""
    cx = x
    for label, color, shape in items:
        if shape == "box":
            svg.rect(cx, y - 9, 12, 12, color, 0.7)
        elif shape == "line":
            svg.line(cx, y - 3, cx + 14, y - 3, color, 2)
        else:
            svg.marker(shape, cx + 6, y - 3, 4, color)
        svg.text(cx + 18, y + 1, label, 11, fill=INK2)
        cx += 18 + 6.2 * len(label) + gap


def hist_chart(path, title, panels, xmin, xmax, bw, xlabel, band=None, cols=2, pw=420, ph=170, legend_items=None,
               xtick=None, ylabel="% of runs", desc="", integer=False):
    rows_n = (len(panels) + cols - 1) // cols
    top, left = 70, 54
    W = cols * (pw + 30) + 30
    H = top + rows_n * (ph + 84) + 10
    svg = Svg(W, H, title, desc)
    svg.text(16, 24, title, 15, fill=INK, weight="bold")
    if legend_items:
        legend(svg, 16, 48, legend_items)
    nb = int(round((xmax - xmin) / bw))
    for idx, pan in enumerate(panels):
        ox = 14 + (idx % cols) * (pw + 30) + left
        oy = top + (idx // cols) * (ph + 84) + 24
        iw = pw - left + 10
        counts_all = []
        outside = []
        for s in pan["series"]:
            vals = [v for v in s["vals"] if not math.isnan(v)]
            c = [0] * nb
            out = 0
            for v in vals:
                k = int(math.floor((v - xmin) / bw + 1e-9))
                if 0 <= k < nb:
                    c[k] += 1
                else:
                    out += 1
            n = max(len(vals), 1)
            counts_all.append([100.0 * x / n for x in c])
            outside.append((out, len(vals)))
        ymax = min(nice_ceiling(max([max(c) for c in counts_all] + [1e-9]) * 1.05), 100.0)
        X = lambda v: ox + (v - xmin) / (xmax - xmin) * iw
        Y = lambda v: oy + ph - v / ymax * ph
        svg.text(ox, oy - 10, pan["label"], 12, fill=INK, weight="bold")
        if band:
            svg.rect(X(band[0]), oy, X(band[1]) - X(band[0]), ph, BAND, 1.0, title="firmware tolerance band")
        for k in range(5):
            yv = ymax * k / 4
            svg.line(ox, Y(yv), ox + iw, Y(yv))
            svg.text(ox - 6, Y(yv) + 4, ("%g" % round(yv, 2)), 10, "end")
        xt = xtick or (xmax - xmin) / 6
        v = xmin + bw / 2 if integer else xmin
        while v <= xmax + 1e-9:
            svg.line(X(v), oy + ph, X(v), oy + ph + 4, INK2)
            svg.text(X(v), oy + ph + 16, ("%g" % round(v, 4)), 10, "middle")
            v += xt
        svg.line(ox, oy + ph, ox + iw, oy + ph, INK2)
        svg.text(ox + iw / 2, oy + ph + 32, xlabel, 11, "middle")
        svg.text(ox - 40, oy + ph / 2, ylabel, 10, "middle", rotate="-90")
        for si, s in enumerate(pan["series"]):
            c = counts_all[si]
            if s.get("kind", "fill") == "fill":
                for k in range(nb):
                    if c[k] > 0:
                        svg.rect(ox + k * iw / nb + 0.5, Y(c[k]), iw / nb - 1, oy + ph - Y(c[k]), s["color"], 0.6,
                                 title="%s: %.1f%% in [%g, %g)" % (s["name"], c[k], xmin + k * bw, xmin + (k + 1) * bw))
            else:
                pts = []
                for k in range(nb):
                    x0, x1 = ox + k * iw / nb, ox + (k + 1) * iw / nb
                    pts.append("%.1f,%.1f %.1f,%.1f" % (x0, Y(c[k]), x1, Y(c[k])))
                svg.add('<polyline points="%s" fill="none" stroke="%s" stroke-width="2"/>' % (" ".join(pts), s["color"]))
        if band is None or xmin < 0 < xmax:
            pass
        note = "; ".join("%s: n=%d, %d outside axis" % (s["name"], outside[i][1], outside[i][0])
                         for i, s in enumerate(pan["series"]))
        svg.text(ox, oy + ph + 46, note, 9.5, fill=INK2)
    svg.save(path)


def stacked_outcomes(path, title, rows, desc=""):
    """rows: list of (label, Counter of OUTCOME_CATS)."""
    lw, bw_, top = 310, 520, 92
    rh = 28
    W = lw + bw_ + 60
    H = top + rh * len(rows) + 40
    svg = Svg(W, H, title, desc)
    svg.text(16, 24, title, 15, fill=INK, weight="bold")
    # two-row legend
    items = list(OUTCOME_CATS.items())
    legend(svg, 16, 48, [(k, c, "box") for k, c in items[:4]])
    legend(svg, 16, 66, [(k, c, "box") for k, c in items[4:]])
    for i, (label, cnt) in enumerate(rows):
        y = top + i * rh
        total = sum(cnt.values()) or 1
        svg.text(lw - 8, y + 15, "%s (n=%d)" % (label, sum(cnt.values())), 11, "end", fill=INK)
        x = lw
        for k, color in OUTCOME_CATS.items():
            n = cnt.get(k, 0)
            if n == 0:
                continue
            w = bw_ * n / total
            svg.rect(x, y, max(w - 2, 1), 20, color, title="%s: %d (%.1f%%)" % (k, n, 100.0 * n / total), rx=2)
            if w > 34:
                svg.text(x + (w - 2) / 2, y + 14, "%.0f%%" % (100.0 * n / total), 10, "middle",
                         fill="#ffffff" if color not in (C_YELLOW, C_AQUA, C_MAGENTA, C_GREY) else INK, weight="bold")
            x += w
    svg.text(lw, top + rh * len(rows) + 22, "share of runs; hover a segment for counts. Segments under 34 px are unlabelled.",
             10, fill=INK2)
    svg.save(path)


def rundry_chart(path, panels, target, title, desc=""):
    """panels: list of (label, rows). Scatter of true cup mass vs beans loaded, coloured by result.
    Top row: full range. Bottom row: zoom on target +- 0.2 g so the tolerance band is visible."""
    marks = {"SUCCESS": (C_BLUE, "circle"), "OVERSHOOT": (C_MAGENTA, "diamond"), "MAX_PULSES": (C_YELLOW, "square"),
             "TIMEOUT": (C_VIOLET, "triangle"), "ERROR": (C_RED, "cross"), "SCALE_ERROR": (C_SLATE, "plus"),
             "NONE": (C_GREY, "circle")}
    allv = [fnum(r["true_cup_g"]) for _, rs in panels for r in rs if not math.isnan(fnum(r["true_cup_g"]))]
    full = (math.floor(min(allv + [target]) - 0.5), math.ceil(max(allv + [target]) + 0.5))
    zoom = (target - 0.2, target + 0.2)
    xmin, xmax = 16.5, 22.5
    pw, ph, left, top = 300, 250, 54, 92
    W = 16 + len(panels) * (pw + 40) + 10
    H = top + 2 * (ph + 70) + 30
    svg = Svg(W, H, title, desc)
    svg.text(16, 24, title, 15, fill=INK, weight="bold")
    legend(svg, 16, 48, [(k, marks[k][0], marks[k][1]) for k in ("SUCCESS", "OVERSHOOT", "MAX_PULSES", "TIMEOUT")])
    legend(svg, 16, 66, [(k, marks[k][0], marks[k][1]) for k in ("ERROR", "SCALE_ERROR", "NONE")] +
           [("target", INK, "line"), ("tolerance band", BAND, "box")])
    for row, (ymin, ymax, rlabel) in enumerate(((full[0], full[1], "full range"), (zoom[0], zoom[1], "zoom: target +- 0.2 g"))):
        oy = top + row * (ph + 70)
        for idx, (label, rs) in enumerate(panels):
            ox = 16 + idx * (pw + 40) + left - 16
            X = lambda v: ox + (v - xmin) / (xmax - xmin) * pw
            Y = lambda v: oy + ph - (v - ymin) / (ymax - ymin) * ph
            svg.text(ox, oy - 10, "%s (%s)" % (label, rlabel), 12, fill=INK, weight="bold")
            svg.rect(ox, Y(target + TOL_G), pw, Y(target - TOL_G) - Y(target + TOL_G) + 0.001, BAND, 1.0)
            if row == 0:
                ticks = [float(v) for v in range(int(ymin), int(ymax) + 1)]
            else:
                ticks = [round(target - 0.2 + 0.05 * k, 2) for k in range(9)]
            for yv in ticks:
                svg.line(ox, Y(yv), ox + pw, Y(yv))
                if idx == 0:
                    svg.text(ox - 6, Y(yv) + 4, "%g" % yv, 10, "end")
            for xv in (17, 18, 19, 20, 21, 22):
                svg.line(X(xv), oy + ph, X(xv), oy + ph + 4, INK2)
                svg.text(X(xv), oy + ph + 16, xv, 10, "middle")
            svg.line(ox, oy + ph, ox + pw, oy + ph, INK2)
            svg.line(ox, Y(target), ox + pw, Y(target), INK, 1.5, "5,3")
            if row == 0:
                lo_, hi_ = max(xmin, ymin), min(xmax, ymax)
                svg.line(X(lo_), Y(lo_), X(hi_), Y(hi_), C_SLATE, 1, "2,3")
            outside = 0
            for r in rs:
                y = fnum(r["true_cup_g"])
                if math.isnan(y):
                    continue
                if not ymin <= y <= ymax:
                    outside += 1
                    continue
                jit = (((int(r["seed"]) * 2654435761) % 1000) / 1000.0 - 0.5) * 0.3
                col, shp = marks.get(r["result"], marks["NONE"])
                svg.marker(shp, X(fnum(r["loaded_g"]) + jit), Y(y), 3, col)
            svg.text(ox + pw / 2, oy + ph + 34, "beans loaded (g); x jittered within +-0.15 g", 11, "middle")
            if row == 1:
                svg.text(ox, oy + ph + 50, "%d runs outside this y range not drawn" % outside, 9.5)
            if idx == 0:
                svg.text(ox - 40, oy + ph / 2, "true grounds in cup (g)", 11, "middle", rotate="-90")
    svg.text(16, H - 10, "Dotted diagonal (full range): every gram loaded ended in the cup. Shaded band: target +- 0.03 g.", 10)
    svg.save(path)


def targets_chart(path, groups, title, desc=""):
    """groups: list of (target, [err_true values], [err_fw values])"""
    allv = [v for _, e, f in groups for v in e + f if not math.isnan(v)]
    lo = max(min(allv + [-TOL_G]) - 0.05, -1.0)
    hi = min(max(allv + [TOL_G]) + 0.05, 1.0)
    pw, ph, left, top = 760, 280, 60, 80
    W, H = left + pw + 30, top + ph + 70
    svg = Svg(W, H, title, desc)
    svg.text(16, 24, title, 15, fill=INK, weight="bold")
    legend(svg, 16, 48, [("true error per run", C_BLUE, "circle"), ("firmware-reported error per run", C_ORANGE, "diamond"),
                         ("median", INK, "line")])
    ox, oy = left, top
    Y = lambda v: oy + ph - (v - lo) / (hi - lo) * ph
    svg.rect(ox, Y(TOL_G), pw, Y(-TOL_G) - Y(TOL_G), BAND, 1.0)
    step = 0.05 if hi - lo < 0.6 else 0.1
    v = math.ceil(lo / step) * step
    while v <= hi + 1e-9:
        svg.line(ox, Y(v), ox + pw, Y(v))
        svg.text(ox - 6, Y(v) + 4, "%+.2f" % v, 10, "end")
        v += step
    svg.line(ox, Y(0), ox + pw, Y(0), INK2, 1)
    gw = pw / len(groups)
    for i, (t, e, f) in enumerate(groups):
        cx = ox + gw * (i + 0.5)
        svg.text(cx, oy + ph + 16, "%g g" % t, 11, "middle", fill=INK)
        for j, v in enumerate(e):
            if not math.isnan(v):
                svg.marker("circle", cx - 14 + ((j * 37) % 11 - 5) * 1.3, Y(max(min(v, hi), lo)), 2.5, C_BLUE)
        for j, v in enumerate(f):
            if not math.isnan(v):
                svg.marker("diamond", cx + 14 + ((j * 29) % 11 - 5) * 1.3, Y(max(min(v, hi), lo)), 2, C_ORANGE)
        for vals, off in ((e, -14), (f, 14)):
            vv = [x for x in vals if not math.isnan(x)]
            if vv:
                m = pct(vv, 0.5)
                svg.line(cx + off - 12, Y(m), cx + off + 12, Y(m), INK, 2)
    svg.text(ox + pw / 2, oy + ph + 40, "target mass", 11, "middle")
    svg.text(16, oy + ph / 2, "error (g)", 11, "middle", rotate="-90")
    svg.text(16, H - 10, "Error = grams minus target. Shaded band = firmware tolerance +-0.03 g. Values clipped to axis.", 10)
    svg.save(path)


# ================================================================== source citations
def cite_define(rel, name):
    p = ROOT / rel
    try:
        for i, line in enumerate(p.read_text().splitlines(), 1):
            if re.match(r"\s*#define\s+%s\b" % re.escape(name), line):
                return "%s:%d" % (rel, i)
    except OSError:
        pass
    return "[TO CONFIRM]"


def cite_text(rel, needle):
    p = ROOT / rel
    try:
        for i, line in enumerate(p.read_text().splitlines(), 1):
            if needle in line:
                return "%s:%d" % (rel, i)
    except OSError:
        pass
    return "[TO CONFIRM]"


def define_value(rel, name):
    p = ROOT / rel
    try:
        for line in p.read_text().splitlines():
            m = re.match(r"\s*#define\s+%s\s+([0-9.]+)" % re.escape(name), line)
            if m:
                return m.group(1)
    except OSError:
        pass
    return "[TO CONFIRM]"


# ================================================================== report
def md_table(header, rows):
    out = ["| " + " | ".join(header) + " |", "|" + "|".join("---" for _ in header) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(c) for c in r) + " |")
    return "\n".join(out)


def err_stats(rs, key="err_true_g"):
    v = [fnum(r[key]) for r in rs if r.get("terminal") in ("COMPLETED", "TIMEOUT")]
    v = [x for x in v if not math.isnan(x)]
    return v


def build_report(sets, results, quick, outdir, report_dir, interpretation_fn):
    outdir_rel = "sim/mc/out/" + ("quick" if quick else "full")
    defaults = load_defaults()
    n_place = sum(1 for d in defaults.values() if d.get("source") == "placeholder")
    n_params = len(defaults)
    sha = hashlib.sha256(GRINDSIM.read_bytes()).hexdigest()[:12] if GRINDSIM.exists() else "[TO CONFIRM]"
    fw = lambda name: cite_define("src/config/grind_control.h", name)
    L = []
    w = L.append
    scen_by_key = {st["key"]: st for st in sets}
    all_rows = {k: results[k] for k in results}
    prim = {k: primary_rows(v) for k, v in all_rows.items()}
    total_runs = sum(len(v) for v in prim.values())

    w("# Monte Carlo report: grind controller in the digital twin")
    w("")
    if quick:
        w("> **QUICK RUN.** Small N for a fast check; numbers are not the full-run numbers. Run "
          "`python3 sim/mc/run_mc.py` for the committed report.")
        w("")
    w("Generated by `sim/mc/run_mc.py`; `grindsim` binary sha256 prefix `%s`. The report contains no timestamps or "
      "wall times, so the same binary and seeds reproduce it exactly." % sha)
    w("")
    w("Everything below is **model output**. The plant (grinder, grounds path, load cell, HX711) is a model in which %d of "
      "its %d parameters are placeholders (`source` = `placeholder` in `sim/plant/params_default.json`); none has been "
      "measured on real hardware. Nothing here is a statement about a real grinder." % (n_place, n_params))
    w("")

    # ---------------------------------------------------------------- headline
    w("## 1. Summary of runs")
    w("")
    hdr = ["Set", "Seeds (first..last)", "Scenario files", "Runs", "Failures", "Failure %"]
    rows = []
    fail_index = {}  # computed below; placeholder to keep order
    w("@@SUMMARY_TABLE@@")
    w("")
    w("Total runs (one per seed and scenario file): **%d**. A *failure* and the *SAFETY* tags are defined in section 2.3." % total_runs)
    w("")

    # ---------------------------------------------------------------- method
    w("## 2. Method")
    w("")
    w("### 2.1 What runs")
    w("")
    w("Each run boots the unmodified firmware (all tasks and the LVGL UI) on a deterministic virtual clock against the "
      "plant model (`sim/out/host/grindsim`, see `sim/ARCHITECTURE.md`). A scripted operator loads beans, places the cup, "
      "taps START on the virtual touchscreen, handles the purge prompt, waits for the result and taps OK. The same seed "
      "and inputs give byte-identical output. Batch sets use `grindsim --batch N --seed S --jobs 4`; sets with per-run "
      "parameters or per-run logs run one `grindsim` process per seed (4 in parallel).")
    w("")
    rows = []
    for st in sets:
        n = SIZES[st["seeds"]][1 if quick else 0]
        s0 = SEED_BASE[st["seeds"]]
        files = st["scenarios"]
        fdesc = "`%s`" % files[0]["rel"] if len(files) == 1 else "%d files `sim/mc/scenarios/%s*`" % (
            len(files), os.path.commonprefix([f["stem"] for f in files]))
        rows.append([st["title"], fdesc, "%d..%d (%d per file)" % (s0, s0 + n - 1, n), len(prim[st["key"]]), st["desc"]])
    w(md_table(["Set", "Scenario file(s)", "Seeds", "Runs", "What varies"], rows))
    w("")
    w("Seeds are fixed in `run_mc.py` (`SEED_BASE`); every scenario file of a set uses the same seeds (paired design). "
      "Sweep parameter draws use `random.Random(\"mc-params-v1-<seed>\")` over the parameters sorted by name, uniform in "
      "[min, max] from `params_default.json`; the sampled values of every sweep run are in "
      "`sim/reports/sweep_params.csv` (and as `--params` files under `%s/params/`). "
      "`python3 sim/mc/run_mc.py --emit-params sweep <seed>` prints one run's parameter file." % outdir_rel)
    w("")
    w("**Held at default in the sweeps:** `hx711_sps` (10; 80 SPS is its own set), `lc_counts_per_g` and `lc_baseline_code` "
      "(calibrated away: the operator seeds the firmware calibration from the plant's own value), and "
      "`hx711_settle_ms_80sps` (inactive at 10 SPS). **Deviation from the task text:** the sweep set holds "
      "`lc_gain_error` at 0 and a second set (`sweep_gain`) samples it. Reason (inference): a +-5 %% gain error on a "
      "scale the firmware believes is calibrated shifts an 18 g result by up to +-0.9 g, i.e. far outside the +-0.03 g band "
      "for almost every draw, which would hide every other sensitivity. Both are reported.")
    w("")
    w("### 2.2 What the twin is and is not")
    w("")
    w("- Is: the real `src/` controller, state machine, weight-sensor pipeline and UI running against a dt-stepped physical "
      "model (relay and motor lag, grounds transport and chute retention, burr run-dry taper, load cell dynamics, HX711 "
      "cadence and noise, cup and bump transients).")
    w("- Is not: a measurement of any real grinder or load cell. Most plant parameters are placeholders chosen to land on the "
      "firmware's own mock timings or on plausibility (`sim/plant/ASSUMPTIONS_PLANT.md`), so absolute error, pulse-count and "
      "time numbers describe the model, and only the comparison between scenarios and the fault behaviours of the firmware "
      "logic are informative about the code.")
    w("- Both ESP32 cores are serialised on a cooperative scheduler and firmware code takes zero virtual time "
      "(`sim/ARCHITECTURE.md`), so races between cores are not reproduced. BLE, Wi-Fi and storage are stubs or in memory.")
    w("- \"True\" quantities (`err_true_g`) come from the plant's mass bookkeeping, not from the firmware's scale reading. "
      "`true_cup_g` is sampled about 3 s after the grind is dismissed.")
    w("")
    w("### 2.3 Definitions used in this report")
    w("")
    tol_cite = fw("GRIND_ACCURACY_TOLERANCE_G")
    w("- **Tolerance band** +-%.2f g: `GRIND_ACCURACY_TOLERANCE_G` (%s, value %s)." % (TOL_G, tol_cite,
                                                                                     define_value("src/config/grind_control.h", "GRIND_ACCURACY_TOLERANCE_G")))
    w("- **Error** `err_true_g` = true grounds in the cup (plant) minus target; `err_fw_g` = weight the UI showed as the result "
      "minus target.")
    w("- **Overshoot** (this report's term) = `err_true_g` > +0.03 g; **undershoot** = `err_true_g` < -0.03 g. The firmware's own "
      "`OVERSHOOT` result is counted separately.")
    w("- **Failure** = result not `SUCCESS`, or |`err_true_g`| > 0.03 g, or a SAFETY tag. Rows with `grind` >= 1000 "
      "(post-reset observation after a simulated reset) are not graded for result or error; they are only checked for SAFETY tags.")
    w("- **SAFETY-SIGNAL** = `motor_invalid_signal_s` > %.1f s (motor ran while the load cell was unplugged or stuck) or "
      "`motor_no_sample_s` > %.1f s (motor ran with no fresh firmware sample). %.1f s is the firmware's own freshness window "
      "`has_recent_sample()` (%s)." % (SIGNAL_LIMIT_S, SIGNAL_LIMIT_S, SIGNAL_LIMIT_S,
                                       cite_text("src/hardware/WeightSensor.h", "bool has_recent_sample")))
    w("- **SAFETY-TIME** = `motor_max_run_s` > %d s (longest continuous relay-closed run; `GRIND_TIMEOUT_SEC`, %s, value %s) or "
      "`motor_after_end_s` > %.1f s (motor still running after the firmware ended the session). The %.1f s threshold for "
      "the second condition is this report's own choice, not a firmware constant."
      % (TIME_LIMIT_S, fw("GRIND_TIMEOUT_SEC"), define_value("src/config/grind_control.h", "GRIND_TIMEOUT_SEC"),
         AFTER_END_LIMIT_S, AFTER_END_LIMIT_S))
    w("- SAFETY tags measure the **plant's relay contact**, so with a `relay_stuck_on` fault the motor keeps running whatever "
      "the firmware commands; such rows are tagged because the motor ran, not because the firmware is shown to have "
      "commanded it (see observations).")
    w("- Other firmware constants cited: `GRIND_MAX_PULSE_ATTEMPTS` (%s, value %s), `GRIND_DRY_RUN_TIMEOUT_MS` (%s, value %s), "
      "`GRIND_DRY_RUN_MIN_PROGRESS_G` (%s, value %s), `GRIND_PURGE_AMOUNT_DEFAULT_G` (%s, value %s)."
      % (fw("GRIND_MAX_PULSE_ATTEMPTS"), define_value("src/config/grind_control.h", "GRIND_MAX_PULSE_ATTEMPTS"),
         fw("GRIND_DRY_RUN_TIMEOUT_MS"), define_value("src/config/grind_control.h", "GRIND_DRY_RUN_TIMEOUT_MS"),
         fw("GRIND_DRY_RUN_MIN_PROGRESS_G"), define_value("src/config/grind_control.h", "GRIND_DRY_RUN_MIN_PROGRESS_G"),
         fw("GRIND_PURGE_AMOUNT_DEFAULT_G"), define_value("src/config/grind_control.h", "GRIND_PURGE_AMOUNT_DEFAULT_G")))
    w("")

    # ---------------------------------------------------------------- results
    w("## 3. Results (observations)")
    w("")
    acc_keys = [k for k in ("nominal", "sweep", "sweep_gain", "purge_keep", "prime", "sps80", "sps30") if k in prim]

    w("### 3.1 Outcome per scenario set")
    w("")
    cat_rows = []
    cats = list(OUTCOME_CATS.keys())
    stack_rows = []
    for k in acc_keys:
        cnt = Counter(outcome_cat(r) for r in prim[k])
        stack_rows.append((scen_by_key[k]["title"], cnt))
        cat_rows.append([scen_by_key[k]["title"], len(prim[k])] + [cnt.get(c, 0) for c in cats])
    tg = scen_by_key["targets"]
    for s in tg["scenarios"]:
        rs = [r for r in prim["targets"] if r["_file"] == s["stem"]]
        cnt = Counter(outcome_cat(r) for r in rs)
        stack_rows.append(("Target %d g" % s["meta"]["target"], cnt))
        cat_rows.append(["Target %d g" % s["meta"]["target"], len(rs)] + [cnt.get(c, 0) for c in cats])
    w(md_table(["Set", "Runs"] + [c.replace(" (true err", "<br>(true err") for c in cats], cat_rows))
    w("")
    stacked_outcomes(report_dir / "outcomes.svg", "Outcome per scenario set (overshoot = true error > +0.03 g)", stack_rows,
                     "Stacked share of runs by outcome for each scenario set.")
    w("![Outcome per scenario set](outcomes.svg)")
    w("")

    w("### 3.2 Final error against the firmware tolerance band")
    w("")
    w("`err_true_g` is the plant's true grounds in the cup minus target; `err_fw_g` is what the UI showed minus target. "
      "Statistics use runs that reached COMPLETED or TIMEOUT.")
    w("")
    st_rows = []
    panels = []
    for k in acc_keys:
        t = err_stats(prim[k], "err_true_g")
        f = err_stats(prim[k], "err_fw_g")
        pc = lambda n_: "%.1f" % (100.0 * n_ / len(t)) if t else "n/a"
        fm = [fnum(r["fw_minus_true_g"]) for r in prim[k] if r.get("terminal") in ("COMPLETED", "TIMEOUT")]
        st_rows.append([scen_by_key[k]["title"], len(t), f3(mean(t), 4, True), f3(sd(t), 4), f3(pct(t, .05), 3, True),
                        f3(pct(t, .5), 3, True), f3(pct(t, .95), 3, True), f3(min(t) if t else float("nan"), 3, True),
                        f3(max(t) if t else float("nan"), 3, True),
                        pc(sum(1 for x in t if abs(x) <= TOL_G + 1e-9)), pc(sum(1 for x in t if x > TOL_G + 1e-9)),
                        pc(sum(1 for x in t if x < -TOL_G - 1e-9)),
                        f3(mean(f), 4, True), "%.1f" % (100.0 * sum(1 for x in f if abs(x) <= TOL_G + 1e-9) / len(f)) if f else "n/a",
                        f3(mean(fm), 4, True), f3(sd(fm), 4)])
        if t:
            panels.append(dict(label="%s (n=%d)" % (scen_by_key[k]["title"], len(t)), key=k,
                               series=[dict(name="true", vals=t, color=C_BLUE, kind="fill"),
                                       dict(name="firmware", vals=f, color=C_ORANGE, kind="step")]))
    w(md_table(["Set", "n", "true mean (g)", "true sd (g)", "true p5", "true p50", "true p95", "true min", "true max",
                "true in band %", "overshoot %", "undershoot %", "fw mean (g)", "fw in band %",
                "fw - true mean (g)", "fw - true sd (g)"], st_rows))
    w("")
    xr = 0.3
    other = [p for p in panels if p["key"] not in ("sweep", "sweep_gain")]
    swp = [p for p in panels if p["key"] in ("sweep", "sweep_gain")]
    w("Size of the true error, share of graded runs by |err_true_g| (how far outside the band the failing runs are):")
    w("")
    brow = []
    edges = [(0.03, "<= 0.03"), (0.05, "0.03 to 0.05"), (0.10, "0.05 to 0.10"), (0.25, "0.10 to 0.25"), (1.0, "0.25 to 1.0"),
             (1e9, "> 1.0")]
    for k in acc_keys:
        t = [abs(x) for x in err_stats(prim[k], "err_true_g")]
        if not t:
            continue
        cnt_b = [sum(1 for x in t if x <= 0.03 + 1e-9)]
        for (lo_, _), (hi_, _) in zip(edges[:-1], edges[1:]):
            cnt_b.append(sum(1 for x in t if lo_ + 1e-9 < x <= hi_ + 1e-9))
        brow.append([scen_by_key[k]["title"], len(t)] + ["%.1f" % (100.0 * c / len(t)) for c in cnt_b])
    w(md_table(["Set", "n"] + ["%s g (%%)" % e[1] for e in edges], brow))
    w("")
    w("Where the beans went: mean mass ledger at dismissal (g), from the plant's bookkeeping. `spilled` includes the cup contents "
      "tipped out at the purge prompt; `other` = loaded - (cup + spilled + chute + burr chamber + platform) and should be about 0 "
      "(hopper remainder or grounds in flight would show here).")
    w("")
    lrows = []
    for k in acc_keys:
        rs_ = [r for r in prim[k] if r.get("terminal") in ("COMPLETED", "TIMEOUT")]
        if not rs_:
            continue
        g_ = lambda key: mean([fnum(r[key]) for r in rs_])
        other_ = mean([fnum(r["loaded_g"]) - fnum(r["true_cup_g"]) - fnum(r["spilled_g"]) - fnum(r["chute_g"]) -
                       fnum(r["burr_left_g"]) - fnum(r["platform_g"]) for r in rs_])
        lrows.append([scen_by_key[k]["title"], len(rs_), f3(g_("loaded_g"), 2), f3(g_("true_cup_g"), 3), f3(g_("spilled_g"), 2),
                      f3(g_("chute_g"), 2), f3(g_("burr_left_g"), 2), f3(g_("platform_g"), 2), f3(other_, 3)])
    w(md_table(["Set", "n", "loaded", "cup", "spilled", "chute", "burr chamber", "platform", "other"], lrows))
    w("")
    w("Sets with no graded grind are omitted from the charts (see 3.10 for the 80 SPS set).")
    w("")
    hist_chart(report_dir / "error_hist.svg", "Final error, true (filled) and firmware-reported (outline)", other, -xr, xr,
               0.01, "error = grams - target (g)", band=(-TOL_G, TOL_G), xtick=0.1,
               legend_items=[("true error (plant)", C_BLUE, "box"), ("firmware-reported error", C_ORANGE, "line"),
                             ("firmware tolerance +-0.03 g", BAND, "box")],
               desc="Histograms of final error with the firmware tolerance band shaded.")
    w("![Final error histograms](error_hist.svg)")
    w("")
    if swp:
        hist_chart(report_dir / "error_hist_sweep.svg", "Final error in the parameter sweeps (wide axis, bin 0.05 g)", swp, -2.0, 1.0,
                   0.05, "error = grams - target (g)", band=(-TOL_G, TOL_G), cols=1, pw=700, xtick=0.5,
                   legend_items=[("true error (plant)", C_BLUE, "box"), ("firmware-reported error", C_ORANGE, "line"),
                                 ("firmware tolerance +-0.03 g (narrower than one bin)", BAND, "box")],
                   desc="Histogram of final error for the parameter sweeps.")
        w("![Final error in the sweeps](error_hist_sweep.svg)")
        w("")

    w("### 3.3 Overshoot")
    w("")
    w("Overshoot here is `err_true_g` > +0.03 g. The stacked bars in 3.1 show its share per set; the table below adds the "
      "firmware's own result classes and the size of the overshoot.")
    w("")
    rows = []
    for k in acc_keys:
        pr = prim[k]
        t = [fnum(r["err_true_g"]) for r in pr if r.get("terminal") in ("COMPLETED", "TIMEOUT")]
        ov = [x for x in t if x > TOL_G + 1e-9]
        fw_ov = sum(1 for r in pr if r["result"] == "OVERSHOOT")
        rows.append([scen_by_key[k]["title"], len(pr), len(ov), "%.1f" % (100.0 * len(ov) / max(len(t), 1)),
                     f3(max(ov) if ov else float("nan"), 3, True), f3(pct(ov, .5) if ov else float("nan"), 3, True), fw_ov])
    w(md_table(["Set", "Runs", "True overshoot runs", "% of graded", "largest true overshoot (g)",
                "median overshoot (g)", "firmware result OVERSHOOT"], rows))
    w("")

    w("### 3.4 Pulse count")
    w("")
    w("Pulse count = number of entries into PULSE_EXECUTE during the grind. The firmware limit is `GRIND_MAX_PULSE_ATTEMPTS` (%s)."
      % fw("GRIND_MAX_PULSE_ATTEMPTS"))
    w("")
    ppan = []
    rows = []
    for k in acc_keys:
        v = [fnum(r["pulses"]) for r in prim[k] if r.get("terminal") in ("COMPLETED", "TIMEOUT")]
        if v:
            ppan.append(dict(label="%s (n=%d)" % (scen_by_key[k]["title"], len(v)),
                             series=[dict(name="pulses", vals=v, color=C_BLUE, kind="fill")]))
        cnt = Counter(int(x) for x in v)
        rows.append([scen_by_key[k]["title"], len(v), f3(mean(v), 2), f3(pct(v, .5), 1), f3(pct(v, .95), 1),
                     int(max(v)) if v else "n/a", ", ".join("%d: %d" % (p, cnt[p]) for p in sorted(cnt))])
    w(md_table(["Set", "n", "mean", "median", "p95", "max", "histogram (pulses: runs)"], rows))
    w("")
    hist_chart(report_dir / "pulses_hist.svg", "Pulse count per grind", ppan, -0.5, 12.5, 1, "pulses", cols=2, xtick=1,
               desc="Distribution of pulse corrections.", integer=True)
    w("![Pulse count](pulses_hist.svg)")
    w("")

    w("### 3.5 Grind time")
    w("")
    w("`grind_time_s` = virtual time from leaving IDLE (START tapped) to COMPLETED or TIMEOUT, **including** the time the "
      "scripted operator takes at the purge prompt (reaction %.1f s plus lifting, tipping and replacing the cup, about 4.4 s "
      "in the discard case) - not the motor run time. `motor_on_s` (relay contact closed) is shown separately. Targeted grinds "
      "time out after `GRIND_TIMEOUT_SEC` (%s)." % (1.2, fw("GRIND_TIMEOUT_SEC")))
    w("")
    tpan = []
    rows = []
    for k in acc_keys:
        v = [fnum(r["grind_time_s"]) for r in prim[k] if r.get("terminal") in ("COMPLETED", "TIMEOUT")]
        m = [fnum(r["motor_on_s"]) for r in prim[k] if r.get("terminal") in ("COMPLETED", "TIMEOUT")]
        if v:
            tpan.append(dict(label="%s (n=%d)" % (scen_by_key[k]["title"], len(v)),
                             series=[dict(name="grind time", vals=v, color=C_BLUE, kind="fill")]))
        rows.append([scen_by_key[k]["title"], len(v), f3(mean(v), 1), f3(pct(v, .05), 1), f3(pct(v, .5), 1),
                     f3(pct(v, .95), 1), f3(max(v) if v else float("nan"), 1), f3(mean(m), 1), f3(max(m) if m else float("nan"), 1)])
    w(md_table(["Set", "n", "mean (s)", "p5", "p50", "p95", "max", "mean motor_on_s", "max motor_on_s"], rows))
    w("")
    hist_chart(report_dir / "time_hist.svg", "Grind time (START to result, incl. purge prompt handling)", tpan, 0, 60, 2,
               "seconds", cols=2, xtick=10, desc="Distribution of grind time.")
    w("![Grind time](time_hist.svg)")
    w("")

    # ---- run-dry
    w("### 3.6 Single-dose run-dry")
    w("")
    rd = scen_by_key["rundry"]
    rdp = prim["rundry"]
    panels_rd = []
    for mk, lab in (("discard", "Purge, cup emptied"), ("keep", "Purge, grounds kept"), ("prime", "Prime")):
        panels_rd.append((lab, [r for r in rdp if "_%s_b" % mk in r["_file"]]))
    rundry_chart(report_dir / "rundry.svg", panels_rd, 18.0, "Final true cup mass against beans loaded (target 18.0 g)",
                 "Scatter of true grounds in the cup against beans loaded, coloured by firmware result.")
    w("![Run-dry](rundry.svg)")
    w("")
    rows = []
    for mk in ("discard", "keep", "prime"):
        for b in (17.0, 17.5, 18.0, 18.5, 19.0, 19.5, 20.0, 20.5, 21.0, 22.0):
            rs = [r for r in rdp if r["_file"] == "rundry_%s_b%.1f" % (mk, b)]
            if not rs:
                continue
            cnt = Counter(outcome_label(r) for r in rs)
            tv = [fnum(r["true_cup_g"]) for r in rs]
            bl = [fnum(r["burr_left_g"]) for r in rs]
            rows.append([mk, "%.1f" % b, len(rs), f3(mean(tv), 3), f3(min(tv), 3), f3(max(tv), 3), f3(mean(bl), 2),
                         "; ".join("%s x%d" % (l, n) for l, n in sorted(cnt.items(), key=lambda kv: -kv[1]))])
    w(md_table(["Purge handling", "beans (g)", "runs", "mean true cup (g)", "min", "max", "mean left in burrs (g)",
                "outcomes"], rows))
    w("")

    # ---- targets
    w("### 3.7 Targets")
    w("")
    groups = []
    rows = []
    for s in tg["scenarios"]:
        rs = [r for r in prim["targets"] if r["_file"] == s["stem"]]
        t = err_stats(rs, "err_true_g")
        f = err_stats(rs, "err_fw_g")
        groups.append((s["meta"]["target"], t, f))
        inb = 100.0 * sum(1 for x in t if abs(x) <= TOL_G + 1e-9) / max(len(t), 1)
        ov = 100.0 * sum(1 for x in t if x > TOL_G + 1e-9) / max(len(t), 1)
        pl = [fnum(r["pulses"]) for r in rs]
        gt = [fnum(r["grind_time_s"]) for r in rs]
        rows.append([s["meta"]["target"], len(rs), f3(mean(t), 4, True), f3(sd(t), 4), f3(min(t) if t else float("nan"), 3, True),
                     f3(max(t) if t else float("nan"), 3, True), "%.1f" % inb, "%.1f" % ov, f3(mean(pl), 2), f3(mean(gt), 1)])
    w(md_table(["target (g)", "n", "true mean err (g)", "sd", "min", "max", "in band %", "overshoot %", "mean pulses",
                "mean grind time (s)"], rows))
    w("")
    targets_chart(report_dir / "targets.svg", groups, "Final error by target mass", "Per-run error for each target mass.")
    w("![Targets](targets.svg)")
    w("")

    # ---- sweep sensitivity
    w("### 3.8 Parameter sweep: rank correlation of error with each sampled parameter")
    w("")
    w("Spearman rank correlation (rho) between each sampled plant parameter and the signed true error, and with |true error|, "
      "over the graded runs of the sweep set (%d runs). This is an association in the sampled model, not a causal "
      "attribution; with |rho| below about %.2f it is not distinguishable from noise at this N (inference: approx. "
      "2/sqrt(n))." % (len(err_stats(prim["sweep"])), 2.0 / math.sqrt(max(len(err_stats(prim["sweep"])), 1))))
    w("")
    sweep_params = {}
    for r in results["sweep"]:
        pf = outdir / "params" / "sweep" / ("%s.json" % r["seed"])
        if r["seed"] not in sweep_params and pf.exists():
            sweep_params[r["seed"]] = json.loads(pf.read_text())
    graded = [r for r in prim["sweep"] if r.get("terminal") in ("COMPLETED", "TIMEOUT") and r["seed"] in sweep_params]
    rows = []
    if len(graded) >= 10:
        names = [n for n in sorted(defaults) if n not in SWEEP_HOLD_DEFAULT and n != "lc_gain_error"]
        ye = [fnum(r["err_true_g"]) for r in graded]
        ya = [abs(x) for x in ye]
        corr = []
        for n in names:
            xv = [sweep_params[r["seed"]][n] for r in graded]
            corr.append((n, spearman(xv, ye), spearman(xv, ya),
                         spearman(xv, [fnum(r["pulses"]) for r in graded]),
                         spearman(xv, [fnum(r["grind_time_s"]) for r in graded])))
        corr.sort(key=lambda c: -max(abs(c[1]) if not math.isnan(c[1]) else 0, abs(c[2]) if not math.isnan(c[2]) else 0))
        for n, a, b, c, d in corr[:12]:
            rows.append([n, f3(a, 2, True), f3(b, 2, True), f3(c, 2, True), f3(d, 2, True)])
    w(md_table(["parameter (top 12 by max(|rho_err|, |rho_abs|))", "rho with true error", "rho with |true error|",
                "rho with pulses", "rho with grind time"], rows))
    w("")
    w("Outcome class against sampled parameters (sweep set). For each class with at least 15 runs, the parameters whose "
      "sampled value differs most from the middle of its sweep range: *position* = mean of (value - min)/(max - min) over the "
      "class (0.5 expected if the class were independent of the parameter), z = (position - 0.5)/(sqrt(1/12)/sqrt(n)). "
      "Association only; classes overlap in cause.")
    w("")
    cls_seeds = defaultdict(list)
    cls_rows = defaultdict(list)
    for r in prim["sweep"]:
        if r["seed"] not in sweep_params:
            continue
        lab = outcome_label(r)
        if lab.startswith("SUCCESS"):
            lab = "SUCCESS (in band)" if lab == "SUCCESS (in band)" else "SUCCESS (outside band)"
        cls_seeds[lab].append(r["seed"])
        cls_rows[lab].append(r)
    rows = []
    for lab, seeds_ in sorted(cls_seeds.items(), key=lambda kv: -len(kv[1])):
        if len(seeds_) < 15:
            continue
        sc_ = []
        for n in sorted(defaults):
            if n in SWEEP_HOLD_DEFAULT or n == "lc_gain_error":
                continue
            d_ = defaults[n]
            pos = [(sweep_params[sd_][n] - d_["min"]) / (d_["max"] - d_["min"]) for sd_ in seeds_]
            m_ = sum(pos) / len(pos)
            sc_.append((abs((m_ - 0.5) / (math.sqrt(1.0 / 12) / math.sqrt(len(pos)))), n, m_,
                        (m_ - 0.5) / (math.sqrt(1.0 / 12) / math.sqrt(len(pos)))))
        sc_.sort(reverse=True)
        cr = cls_rows[lab]
        rows.append([lab, len(seeds_), f3(mean([fnum(r["spilled_g"]) for r in cr]), 2), f3(mean([fnum(r["chute_g"]) for r in cr]), 2),
                     f3(mean([fnum(r["burr_left_g"]) for r in cr]), 2),
                     "; ".join("`%s` position %.2f (z %+.1f)" % (n, m_, z_) for _, n, m_, z_ in sc_[:4])])
    w(md_table(["Outcome class", "n", "mean spilled_g (incl. discarded purge)", "mean chute_g at end", "mean burr_left_g at end",
                "four most displaced parameters"], rows))
    w("")

    # ---- faults
    w("### 3.9 Fault injection")
    w("")
    fl = scen_by_key["faults"]
    fprim = prim["faults"]
    fall = all_rows["faults"]
    post = defaultdict(list)
    for r in fall:
        if int(fnum(r["grind"])) >= 1000:
            post[(r["_file"], r["seed"])].append(r)
    w("Default plant, purge discard, target 18.0 g, 22 g beans unless stated. Faults stay active until the scenario clears "
      "them. *Not injected* = the anchoring phase was never reached, so the injection event did not fire (counted from "
      "the firmware log `[SIM ...] event` lines); those runs are ordinary grinds. Timing columns: pred = after first entry "
      "to PREDICTIVE, pdec = after first entry to PULSE_DECISION, rnd = seconds after START.")
    w("")
    # stacked chart per family
    fam_order = []
    fam_label = {}
    for s in fl["scenarios"]:
        f = s["meta"]["family"]
        if f not in fam_order:
            fam_order.append(f)
            fam_label[f] = s["meta"]["label"]
    fam_rows = []
    for f in fam_order:
        rs = [r for r in fprim if scen_file_family(fl, r["_file"]) == f and (r["_events_total"] == 0 or r["_events_fired"] >= 1)]
        fam_rows.append((fam_label[f], Counter(outcome_cat(r) for r in rs)))
    stacked_outcomes(report_dir / "faults.svg", "Outcome per fault family (runs where the fault was injected)", fam_rows,
                     "Share of outcomes per injected fault family.")
    w("![Faults](faults.svg)")
    w("")
    rows = []
    for s in fl["scenarios"]:
        rs = [r for r in fprim if r["_file"] == s["stem"]]
        if not rs:
            continue
        # events: the 2 event variants (clear) fire later, the first event decides "injected"
        not_inj = sum(1 for r in rs if r["_events_fired"] < 1 and s["meta"]["when"] != "-")
        cnt = Counter(outcome_label(r) for r in rs)
        sig = sum(1 for r in rs if "SAFETY-SIGNAL" in tags_of(r) or any("SAFETY-SIGNAL" in tags_of(p) for p in post[(r["_file"], r["seed"])]))
        tim = sum(1 for r in rs if "SAFETY-TIME" in tags_of(r) or any("SAFETY-TIME" in tags_of(p) for p in post[(r["_file"], r["seed"])]))
        mo = [fnum(r["motor_on_s"]) for r in rs]
        ev = [x for x in (fnum(r["err_true_g"]) for r in rs if r.get("terminal") != "NONE") if not math.isnan(x)]
        rows.append([s["meta"]["label"], s["meta"]["timing"], len(rs), not_inj,
                     "; ".join("%s x%d" % (l, n) for l, n in sorted(cnt.items(), key=lambda kv: -kv[1])),
                     "%s..%s" % (f3(min(ev), 2, True), f3(max(ev), 2, True)) if ev else "n/a", sig, tim, f3(max(mo), 1)])
    w(md_table(["Fault", "timing", "runs", "not injected", "outcomes (count)", "err_true range (g)", "SAFETY-SIGNAL runs",
                "SAFETY-TIME runs", "max motor_on_s"], rows))
    w("")

    # ---- reset post-observation
    w("#### Reset mid-grind: what the firmware did after the reboot")
    w("")
    w("After the simulated reset the operator only watches the rebooted firmware for 20 s (`grind` >= 1000 rows). "
      "`true cup` is the plant's grounds in the cup at the end of the observation; motor_on_s is relay-closed time "
      "after the reboot.")
    w("")
    rows = []
    for s_ in fl["scenarios"]:
        if s_["meta"]["family"] != "reset":
            continue
        ps = [p_ for r in fprim if r["_file"] == s_["stem"] for p_ in post[(r["_file"], r["seed"])]]
        if not ps:
            continue
        ready = sum(1 for p_ in ps if p_.get("operator_status") == "post-reset ready")
        rows.append([s_["meta"]["timing"], len(ps), ready, f3(max(fnum(p_["motor_on_s"]) for p_ in ps), 3),
                     f3(min(fnum(p_["true_cup_g"]) for p_ in ps), 2), f3(max(fnum(p_["true_cup_g"]) for p_ in ps), 2),
                     "; ".join("%s x%d" % (k_, n_) for k_, n_ in Counter((p_.get("operator_status") or "") for p_ in ps).items())])
    w(md_table(["timing", "runs", "rebooted to READY", "max motor_on_s after reboot", "true cup min (g)", "true cup max (g)",
                "operator status"], rows))
    w("")

    # ---- 80 SPS gate
    w("### 3.10 HX711 at 80 SPS")
    w("")
    n80 = len(prim["sps80"])
    st80 = Counter((r.get("operator_status") or "") for r in prim["sps80"])
    w("In the 80 SPS set the scripted operator never reached a grind: operator status over the %d runs: %s. No row has a "
      "grind result (`grind` = -1)." % (n80, "; ".join("`%s` x%d" % (k_, v_) for k_, v_ in st80.items())))
    probe = outdir / "logs" / "sps80_probe.log"
    if probe.exists():
        lines = [l_.strip() for l_ in probe.read_text(errors="replace").splitlines()
                 if "sample rate detected" in l_ or "hardware validation failed" in l_ or "Hardware validation completed" in l_]
        if lines:
            w("")
            w("Firmware log lines of the first seed (`%s`, verbatim, run with `--log`):" % (
                repro_command(scen_by_key["sps80"], scen_by_key["sps80"]["scenarios"][0], SEED_BASE["sps80"], outdir_rel)))
            w("")
            w("```")
            for l_ in lines[:6]:
                w(l_)
            w("```")
    w("")
    w("The firmware rejects the scale when the detected rate exceeds 4 x `HW_LOADCELL_SAMPLE_RATE_SPS` (%s, %s; check at %s), and "
      "`docs/TROUBLESHOOTING.md` states that a high RATE pin \"will now block startup\" (%s). The firmware-accepted "
      "alternative in this report is the 30 SPS model-only set in the tables above."
      % (cite_define("src/config/hardware.h", "HW_LOADCELL_SAMPLE_RATE_SPS"),
         "value " + define_value("src/config/hardware.h", "HW_LOADCELL_SAMPLE_RATE_SPS"),
         cite_text("src/hardware/WeightSensor.cpp", "kSampleRateUpperThreshold ="),
         cite_text("docs/TROUBLESHOOTING.md", "SAMPLE_RATE_INVALID:")))
    w("")

    w("@@INTERPRETATION@@")
    w("")
    w("@@SAFETY@@")
    w("")
    w("@@FAILURES@@")
    w("")
    w("## 7. Limitations, assumptions and unverified items")
    w("")
    w("- **Assumption:** the scripted operator is the only user behaviour modelled (reaction %.1f s, tap hold 0.1 s, 3 s "
      "before dismissing); a real user's timing differs." % 1.2)
    w("- **Assumption:** `err_true_g` is read when the grind is dismissed (about 3 s after the terminal phase) and includes "
      "grounds that arrive after the firmware declared the result; `true_cup_end_g` (at the terminal phase) is in the raw CSVs.")
    w("- **Assumption:** the sweep draws parameters independently and uniformly; real parameters are correlated (for "
      "example flow and bean factor) and some combinations may be physically implausible.")
    w("- **Uncertain:** how closely the plant placeholders (`sim/plant/ASSUMPTIONS_PLANT.md`) resemble any real grinder; all "
      "absolute numbers above depend on them.")
    w("- **Limitation:** faults are injected only at the times in section 2.1, one fault at a time. `motor_stall` and "
      "`feed_block` are permanent unless stated.")
    w("- **Limitation:** the SAFETY tags look at the plant's relay contact and at the firmware-visible sample freshness; they "
      "do not observe internal firmware decisions.")
    w("- **Limitation:** the motor-time metrics (`motor_after_end_s`, `motor_max_run_s`) are only accumulated until the scripted "
      "operator dismisses the grind (about 3 s after the terminal phase) or the run ends, so with a persistent fault such as "
      "`relay_stuck_on` they are lower bounds; the plant motor would keep running.")
    w("- **Limitation:** `err_true_g` is not graded for runs without a terminal phase (reset, operator gave up); those rows show "
      "`n/a` in the error ranges.")
    w("- **Limitation:** a reset re-executes the `grindsim` process with persisted NVS, LittleFS and plant state; RAM is lost as on "
      "a power-on reset (`sim/core/runtime.cpp`).")
    w("")
    text = "\n".join(L)
    return text, prim, all_rows, fam_order


def scen_file_family(faultset, stem):
    for s in faultset["scenarios"]:
        if s["stem"] == stem:
            return s["meta"]["family"]
    return None


def compute_failures(sets, results, outdir_rel):
    """Returns list of dict(set, file, seed, cls, result, error, err, tags, cmd, post)."""
    fails = []
    scen_lookup = {}
    for st in sets:
        for s in st["scenarios"]:
            scen_lookup[(st["key"], s["stem"])] = (st, s)
    for key, rows in results.items():
        by_run = defaultdict(list)
        for r in rows:
            by_run[(r["_file"], int(r["seed"]))].append(r)
        for (fname, seed), rs in sorted(by_run.items(), key=lambda kv: (kv[0][0], kv[0][1])):
            st, s = scen_lookup[(key, fname)]
            parts, tags = [], []
            pr = [r for r in rs if int(fnum(r["grind"])) < 1000]
            po = [r for r in rs if int(fnum(r["grind"])) >= 1000]
            cls = None
            if pr:
                r0 = pr[0]
                if r0.get("result") == "CRASH":
                    cls = "CRASH"
                    tags_p = []
                else:
                    sig = failure_signature(r0)
                    tags_p = tags_of(r0)
                    cls = sig[0] if sig and sig[0] not in ("SUCCESS",) and not (sig[0] == "SUCCESS (in band)") else None
                    if sig and cls is None and tags_p:
                        cls = outcome_label(r0) + " (tagged only)"
                tags += tags_p
            for p in po:
                tags += ["post-reset:" + t for t in tags_of(p)]
            if cls is None and not tags:
                continue
            if cls is None:
                cls = outcome_label(pr[0]) if pr else "no row"
            r0 = pr[0] if pr else rs[0]
            fails.append(dict(set=key, file=fname, seed=seed, cls=cls, result=r0.get("result"), error=(r0.get("error") or "").strip(),
                              err=fnum(r0.get("err_true_g")) if r0.get("terminal") != "NONE" else float("nan"), tags=sorted(set(tags)),
                              cmd=repro_command(st, s, seed, outdir_rel),
                              vals={k: fnum(r0.get(k)) for k in ("motor_invalid_signal_s", "motor_no_sample_s", "motor_max_run_s",
                                                                 "motor_after_end_s")}))
    return fails


def sections_failures(sets, fails, outdir_rel, prim, quick):
    scen_lookup = {(st["key"], s["stem"]): (st, s) for st in sets for s in st["scenarios"]}
    L = []
    w = L.append
    w("## 6. Failure cases")
    w("")
    w("Failure definition: section 2.3. **Every failing seed is listed.** Rows group seeds that have the same scenario file "
      "and the same cause signature (result and firmware error text, or overshoot/undershoot, plus SAFETY tags); "
      "a range `a-b` includes every seed from a to b. The exact command for any one seed is the template in the "
      "*Reproduce* column with `<SEED>` replaced; the per-seed list with fully expanded commands is in "
      "`sim/reports/montecarlo_failures.csv`. `G` = `%s` and all commands run from the repository root." % GRINDSIM_REL)
    w("")
    if not fails:
        w("No failures.")
        return "\n".join(L)
    by_set = defaultdict(list)
    for f in fails:
        by_set[f["set"]].append(f)
    for st in sets:
        fs = by_set.get(st["key"], [])
        n_runs = len(prim[st["key"]])
        w("### 6.%d %s: %d failing of %d runs" % (sets.index(st) + 1, st["title"], len({(f["file"], f["seed"]) for f in fs}), n_runs))
        w("")
        if not fs:
            w("No failures.")
            w("")
            continue
        groups = OrderedDict()
        for f in fs:
            key = (f["file"], f["cls"], tuple(f["tags"]))
            groups.setdefault(key, []).append(f)
        rows = []
        for (fname, cls, tags), items in groups.items():
            _, s = scen_lookup[(st["key"], fname)]
            tmpl = repro_command(st, s, 0, outdir_rel).replace("--seed 0", "--seed <SEED>").replace("/0.json", "/<SEED>.json")
            tmpl = tmpl.replace(GRINDSIM_REL, "$G")
            errs = [i["err"] for i in items if not math.isnan(i["err"])]
            rows.append(["`%s`" % fname, cls.replace("|", "/"), ", ".join(tags) or "-", len(items),
                         ranges([i["seed"] for i in items]),
                         (f3(min(errs), 3, True) if min(errs) == max(errs) else "%s..%s" % (f3(min(errs), 3, True), f3(max(errs), 3, True))) if errs else "n/a", "`%s`" % tmpl])
        w(md_table(["Scenario file", "Cause signature", "SAFETY tags", "Seeds (count)", "Seed list", "err_true range (g)", "Reproduce (replace <SEED>)"], [
            [r[0], r[1], r[2], r[3], r[4], r[5], r[6]] for r in rows]))
        w("")
    return "\n".join(L)


def sections_safety(sets, fails):
    L = []
    w = L.append
    w("## 5. SAFETY-tagged cases")
    w("")
    w("Tag definitions: section 2.3 (this report's own thresholds). Counts are runs (seeds); the seed lists are in section 6, "
      "in the rows whose *SAFETY tags* column is not `-`.")
    w("")
    tagged = [f for f in fails if f["tags"]]
    if not tagged:
        w("No run carried a SAFETY tag.")
        return "\n".join(L)
    groups = OrderedDict()
    for f in tagged:
        groups.setdefault((f["set"], f["file"], tuple(f["tags"])), []).append(f)
    rows = []
    for (setk, fname, tags), items in groups.items():
        mx = lambda k: max((i["vals"][k] for i in items if not math.isnan(i["vals"][k])), default=float("nan"))
        rows.append(["`%s`" % fname, ", ".join(tags), len(items), f3(mx("motor_invalid_signal_s"), 3), f3(mx("motor_no_sample_s"), 3),
                     f3(mx("motor_max_run_s"), 1), f3(mx("motor_after_end_s"), 3)])
    w(md_table(["Scenario file", "Tags", "Runs", "max motor_invalid_signal_s", "max motor_no_sample_s", "max motor_max_run_s",
                "max motor_after_end_s"], rows))
    w("")
    return "\n".join(L)


def write_failures_csv(fails, path):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["set", "scenario_file", "seed", "cause_signature", "result", "error", "err_true_g", "safety_tags", "reproduce"])
        for f in fails:
            w.writerow([f["set"], f["file"], f["seed"], f["cls"], f["result"], f["error"],
                        "" if math.isnan(f["err"]) else "%.4f" % f["err"], ";".join(f["tags"]), f["cmd"]])


def summary_table(sets, prim, fails):
    rows = []
    fset = defaultdict(set)
    for f in fails:
        fset[f["set"]].add((f["file"], f["seed"]))
    tot_r = tot_f = 0
    for st in sets:
        runs = prim[st["key"]]
        seeds = sorted(int(r["seed"]) for r in runs)
        nf = len(fset[st["key"]])
        tot_r += len(runs)
        tot_f += nf
        rows.append([st["title"], "%d..%d" % (seeds[0], seeds[-1]) if seeds else "-", len(st["scenarios"]), len(runs), nf,
                     "%.1f" % (100.0 * nf / max(len(runs), 1))])
    rows.append(["**All**", "", sum(len(st["scenarios"]) for st in sets), tot_r, tot_f, "%.1f" % (100.0 * tot_f / max(tot_r, 1))])
    return md_table(["Set", "Seeds (first..last)", "Scenario files", "Runs", "Failing runs", "Failing %"], rows)


# ================================================================== main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quick", action="store_true", help="small N for a fast check; output goes to sim/reports/quick/")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--reuse", action="store_true",
                    help="do not run grindsim; rebuild the report from the raw CSVs of the previous run of the same mode")
    ap.add_argument("--emit-params", nargs=2, metavar=("SET", "SEED"), help="print sampled params (SET: sweep|sweep_gain)")
    args = ap.parse_args()

    if args.emit_params:
        print(json.dumps(sample_params(args.emit_params[0], int(args.emit_params[1]), load_defaults()), indent=1, sort_keys=True))
        return 0
    if not GRINDSIM.exists():
        print("missing %s; build it: cmake -S sim/host -B sim/out/host -G Ninja && ninja -C sim/out/host" % GRINDSIM, file=sys.stderr)
        return 2

    mode = "quick" if args.quick else "full"
    outdir = MC_DIR / "out" / mode
    report_dir = REPORT_DIR_FULL / "quick" if args.quick else REPORT_DIR_FULL
    outdir.mkdir(parents=True, exist_ok=True)
    report_dir.mkdir(parents=True, exist_ok=True)
    outdir_rel = "sim/mc/out/" + mode

    sets = build_catalogue()
    write_scenarios(sets)
    t0 = time.time()
    print("run_mc (%s): %d scenario files" % (mode, sum(len(s["scenarios"]) for s in sets)))
    if args.reuse:
        results, timing, sweep_rows = load_previous(sets, args.quick, outdir)
    else:
        results, timing, sweep_rows = execute(sets, args.quick, args.jobs, outdir, print)
    wall = time.time() - t0
    print("runs done in %.1f s" % wall)
    if not args.reuse:
        sc80 = [st for st in sets if st["key"] == "sps80"][0]["scenarios"][0]
        run_single(sc80, SEED_BASE["sps80"], outdir / "logs" / "sps80_probe.log")

    text, prim, all_rows, fam_order = build_report(sets, results, args.quick, outdir, report_dir, None)
    fails = compute_failures(sets, results, outdir_rel)
    write_failures_csv(fails, report_dir / "montecarlo_failures.csv")
    try:
        from interpretation import interpretation_text  # sim/mc/interpretation.py
        interp = interpretation_text(args.quick, hashlib.sha256(GRINDSIM.read_bytes()).hexdigest()[:12])
    except ImportError:
        interp = "## 4. Interpretation\n\n(none)"
    text = text.replace("@@SUMMARY_TABLE@@", summary_table(sets, prim, fails))
    text = text.replace("@@INTERPRETATION@@", interp)
    text = text.replace("@@SAFETY@@", sections_safety(sets, fails))
    text = text.replace("@@FAILURES@@", sections_failures(sets, fails, outdir_rel, prim, args.quick))
    (report_dir / "montecarlo.md").write_text(text + "\n")

    # sampled sweep parameters (raw output kept with the reports)
    if sweep_rows:
        names = [k for k in sweep_rows[0][1].keys() if k != "seed"]
        with open(report_dir / "sweep_params.csv", "w", newline="") as fh:
            wr = csv.writer(fh)
            wr.writerow(["set", "seed"] + names)
            for setk, row in sweep_rows:
                wr.writerow([setk, row["seed"]] + [repr(row[n]) for n in names])
    (outdir / "run_info.json").write_text(json.dumps(
        {"mode": mode, "wall_s": round(wall, 1), "per_set": {k: {"rows": v[0], "wall_s": round(v[1], 1)} for k, v in timing.items()}},
        indent=1) + "\n")
    print("wrote %s" % (report_dir / "montecarlo.md"))
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(MC_DIR))
    sys.exit(main())
