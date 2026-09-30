#!/usr/bin/env bash
# Same seed and parameters must give byte-identical trace, log and summary.
set -euo pipefail
bin="$1"; scenario="$2"; work="$(mktemp -d)"
for run in a b; do
  "$bin" --seed 42 --scenario "$scenario" --out "$work/$run.csv" --log "$work/$run.log" --summary "$work/$run.sum"
done
for ext in csv log sum; do
  if ! cmp -s "$work/a.$ext" "$work/b.$ext"; then echo "determinism: $ext differs"; exit 1; fi
done
echo "determinism: identical ($(sha256sum "$work/a.csv" | cut -c1-16))"
rm -rf "$work"
