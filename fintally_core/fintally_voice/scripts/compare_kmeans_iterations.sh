#!/usr/bin/env bash
set -euo pipefail

# Controlled comparison: independently train fresh 500- and 200-iteration
# codebooks into isolated report directories. Neither run exports runtime files.

ROOT="${FINTALLY_CORE_ROOT:-$HOME/projects/FINTALLY-Finance-Monitoring-app/fintally_core}"
ROOT="$(cd "$ROOT" && pwd)"
cd "$ROOT"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

VOICE="$ROOT/fintally_voice"
BUILDER="$VOICE/offline_builder"
DATA_DIR="$BUILDER/data"
SEED="${FINTALLY_KMEANS_SEED:-252758545}"
COMPARE_ROOT="${FINTALLY_COMPARE_DIR:-$HOME/fintally-kmeans-comparison-$(date +%Y%m%d-%H%M%S)}"

if [[ -e "$COMPARE_ROOT" ]]; then
    printf 'ERROR: comparison output directory already exists; refusing to reuse it:\n  %s\n' "$COMPARE_ROOT" >&2
    printf 'Set FINTALLY_COMPARE_DIR to a new, unused path or rerun without overriding it.\n' >&2
    exit 1
fi

if [[ ! -d "$DATA_DIR" ]]; then
    printf 'ERROR: prepared dataset directory is missing: %s\n' "$DATA_DIR" >&2
    exit 1
fi
# Clap derives the CLI spelling `--no-export` from the Rust field `no_export`
# when it is annotated with `#[arg(long)]`; the literal `--no-export` need not
# appear anywhere in main.rs. Verify the flag field and that the export guard is
# wired into the builder configuration before starting expensive training.
if ! grep -Eq '^[[:space:]]*no_export:[[:space:]]*bool[[:space:]]*,' "$BUILDER/src/main.rs" \
    || ! grep -Fq 'export_codebooks: !cli.no_export' "$BUILDER/src/main.rs" \
    || ! grep -Fq 'if config.export_codebooks' "$BUILDER/src/main.rs"; then
    printf 'ERROR: main.rs is missing the complete no-export wiring (no_export field, config assignment, or export guard).\n' >&2
    exit 1
fi
if [[ ! -f "$SCRIPT_DIR/compare_validation_reports.py" ]]; then
    printf 'ERROR: comparison helper is missing: %s/compare_validation_reports.py\n' "$SCRIPT_DIR" >&2
    exit 1
fi

RUNTIME_FILES=(
    "$VOICE/src/codebooks/fin_voice_codebooks.fvcb"
    "$VOICE/c_core/include/codebooks/fin_voice_codebook_data.h"
    "$VOICE/c_core/src/generated_codebook_data.c"
    "$VOICE/src/fin_voice_codebooks.rs"
)
for path in "${RUNTIME_FILES[@]}"; do
    if [[ ! -f "$path" ]]; then
        printf 'ERROR: expected runtime artifact does not exist: %s\n' "$path" >&2
        exit 1
    fi
done

mkdir -p "$COMPARE_ROOT/500" "$COMPARE_ROOT/200"
printf 'Comparison directory: %s\n' "$COMPARE_ROOT"
printf 'Prepared dataset: %s\n' "$DATA_DIR"
printf 'Fixed K-means seed: %s\n' "$SEED"
printf 'Both runs use CUDA assignment when available and --no-export.\n'
printf 'Runtime artifacts will be checksum-verified before/after.\n\n'

sha256sum "${RUNTIME_FILES[@]}" > "$COMPARE_ROOT/runtime-artifacts-before.sha256"

run_training() {
    local limit="$1"
    local output_dir="$2"
    local log_file="$3"

    printf '\n============================================================\n'
    printf 'Training comparison run: max_iterations=%s\n' "$limit"
    printf 'Output report directory: %s\n' "$output_dir"
    printf '============================================================\n'

    set -o pipefail
    env -u CUDA_VISIBLE_DEVICES \
        RUST_LOG=info \
        LD_LIBRARY_PATH="/usr/lib/wsl/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        cargo run --release -p offline_builder --features cuda -- \
            --raw-dir "$VOICE/offline_builder/data_raw" \
            --data-dir "$DATA_DIR" \
            --output-dir "$output_dir" \
            --sample-rate 16000 \
            --channels 1 \
            --feature-dim 32 \
            --codebook-size 128 \
            --kmeans-iterations "$limit" \
            --kmeans-seed "$SEED" \
            --workers 0 \
            --no-export \
            train 2>&1 | tee "$log_file"

    if [[ ! -f "$output_dir/validation_report.md" ]]; then
        printf 'ERROR: run with limit %s did not create validation_report.md\n' "$limit" >&2
        exit 1
    fi
}

run_training 500 "$COMPARE_ROOT/500" "$COMPARE_ROOT/train-500.log"
run_training 200 "$COMPARE_ROOT/200" "$COMPARE_ROOT/train-200.log"

sha256sum "${RUNTIME_FILES[@]}" > "$COMPARE_ROOT/runtime-artifacts-after.sha256"
if ! diff -u "$COMPARE_ROOT/runtime-artifacts-before.sha256" "$COMPARE_ROOT/runtime-artifacts-after.sha256"; then
    printf '\nERROR: runtime artifact checksums changed during comparison.\n' >&2
    exit 1
fi

printf '\nComparing fresh validation reports...\n'
python3 "$SCRIPT_DIR/compare_validation_reports.py" \
    "$COMPARE_ROOT/500/validation_report.md" \
    "$COMPARE_ROOT/200/validation_report.md"

printf '\nRuntime C/Rust codebook artifact checksums are unchanged.\n'
printf 'Finished. Reports and training logs: %s\n' "$COMPARE_ROOT"
