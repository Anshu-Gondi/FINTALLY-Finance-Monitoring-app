#!/usr/bin/env bash
set -euo pipefail

# Run from any directory; results go into benchmark-results/ under the crate.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CRATE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
cd "$CRATE_DIR"

OUT_DIR="${FINTALLY_BENCH_OUT:-$CRATE_DIR/benchmark-results}"
mkdir -p "$OUT_DIR"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
LOG="$OUT_DIR/rust-benchmark-$STAMP.log"
CSV="$OUT_DIR/rust-benchmark-$STAMP.csv"
ALLOC_CSV="$OUT_DIR/rust-allocation-$STAMP.csv"

{
  echo "=== FinTally Voice Rust runtime benchmark ==="
  echo "utc_timestamp=$STAMP"
  echo "git_commit=$(git rev-parse HEAD 2>/dev/null || echo unavailable)"
  echo "rustc=$(rustc --version 2>/dev/null || echo unavailable)"
  echo "cargo=$(cargo --version 2>/dev/null || echo unavailable)"
  echo "kernel=$(uname -srvm 2>/dev/null || echo unavailable)"
  echo "cpu_model=$(grep -m1 -E 'model name|Hardware' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | xargs || echo unavailable)"
  echo "cpu_governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unavailable)"
  echo "cpu0_freq_khz=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || echo unavailable)"
  echo "cpu_online=$(cat /sys/devices/system/cpu/online 2>/dev/null || echo unavailable)"
  echo "FINTALLY_BENCH_SAMPLES=${FINTALLY_BENCH_SAMPLES:-200}"
  echo "FINTALLY_BENCH_BATCH=${FINTALLY_BENCH_BATCH:-64}"
  echo "FINTALLY_ALLOC_OPS=${FINTALLY_ALLOC_OPS:-10000}"
  echo "=== elapsed-time benchmark (no counting allocator) ==="
  FINTALLY_BENCH_CSV="$CSV" cargo bench -p fintally_voice --bench voice_runtime
  echo "=== allocation-count probe (separate process) ==="
  FINTALLY_ALLOC_CSV="$ALLOC_CSV" cargo bench -p fintally_voice --bench voice_allocations
} 2>&1 | tee "$LOG"

echo "Saved log: $LOG"
echo "Saved timing CSV: $CSV"
echo "Saved allocation CSV: $ALLOC_CSV"
