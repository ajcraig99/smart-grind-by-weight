#!/usr/bin/env bash
# A normal 18.0 g grind with enough beans must complete within the firmware tolerance of the target.
set -euo pipefail
bin="$1"; scenario="$2"; work="$(mktemp -d)"
"$bin" --seed 7 --scenario "$scenario" --summary "$work/s.csv"
python3 - "$work/s.csv" <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
assert len(rows) == 1, rows
r = rows[0]
assert r["terminal"] == "COMPLETED", r
assert r["result"] in ("SUCCESS",), r
assert abs(float(r["fw_final_g"]) - 18.0) <= 0.05, r
assert abs(float(r["err_true_g"])) <= 0.1, r
print("smoke: ok", r["fw_final_g"], r["true_cup_g"], "pulses", r["pulses"])
PY
rm -rf "$work"
