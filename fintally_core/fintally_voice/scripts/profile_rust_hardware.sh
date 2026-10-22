#!/usr/bin/env bash
set -euo pipefail

# Build the benchmark executable, then run it under Linux perf stat so compile
# time is not mixed into the hardware-counter measurements.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CRATE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
cd "$CRATE_DIR"
OUT_DIR="${FINTALLY_BENCH_OUT:-$CRATE_DIR/benchmark-results}"
mkdir -p "$OUT_DIR"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BUILD_JSON="$(mktemp)"
trap 'rm -f "$BUILD_JSON"' EXIT

cargo bench -p fintally_voice --bench voice_runtime --no-run --message-format=json > "$BUILD_JSON"
BENCH_BIN="$(python3 - "$BUILD_JSON" <<'PY'
import json
import sys

executable = None
with open(sys.argv[1], encoding="utf-8") as stream:
    for line in stream:
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        target = item.get("target", {})
        if (item.get("reason") == "compiler-artifact"
                and target.get("name") == "voice_runtime"
                and "bench" in target.get("kind", [])
                and item.get("executable")):
            executable = item["executable"]
if not executable:
    raise SystemExit("Could not locate the voice_runtime bench executable in Cargo metadata")
print(executable)
PY
)"

{
  echo "utc_timestamp=$STAMP"
  echo "benchmark_binary=$BENCH_BIN"
  echo "rustc=$(rustc --version 2>/dev/null || echo unavailable)"
  echo "kernel=$(uname -srvm 2>/dev/null || echo unavailable)"
  echo "cpu_model=$(grep -m1 -E 'model name|Hardware' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | xargs || echo unavailable)"
  echo "cpu_governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unavailable)"
  echo "cpu0_freq_khz=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || echo unavailable)"
} | tee "$OUT_DIR/hardware-$STAMP.txt"

if command -v perf >/dev/null 2>&1; then
  echo "Running perf stat; permission/event availability depends on kernel configuration."
  PERF_REPEATS="${PERF_REPEATS:-5}"
  FINTALLY_BENCH_CSV="$OUT_DIR/perf-run-$STAMP.csv" \
    perf stat -d -d -r "$PERF_REPEATS" -o "$OUT_DIR/perf-stat-$STAMP.txt" -- \
    "$BENCH_BIN" 2>&1 | tee "$OUT_DIR/perf-benchmark-$STAMP.log" || {
      echo "perf stat failed (often permissions or unsupported PMU events). Running an uninstrumented fallback."
      FINTALLY_BENCH_CSV="$OUT_DIR/fallback-run-$STAMP.csv" \
        "$BENCH_BIN" 2>&1 | tee "$OUT_DIR/fallback-benchmark-$STAMP.log"
    }
  if [[ -f "$OUT_DIR/perf-stat-$STAMP.txt" ]]; then
    cat "$OUT_DIR/perf-stat-$STAMP.txt"
  fi
else
  echo "Linux perf not found; running Rust timer/allocation benchmark only."
  FINTALLY_BENCH_CSV="$OUT_DIR/no-perf-run-$STAMP.csv" \
    "$BENCH_BIN" 2>&1 | tee "$OUT_DIR/no-perf-benchmark-$STAMP.log"
fi

echo "Results directory: $OUT_DIR"
