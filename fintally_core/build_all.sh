#!/usr/bin/env bash
set -e  # stop on error

# Define the error log file path
LOG_FILE="build_errors.log"

# Clear out previous build logs on a fresh run
> "$LOG_FILE"

echo "🚀 Starting full build..."

CRATES=(
  "rust_backend"
  "fintally_chatbot"
  "fintally_finance"
)

for crate in "${CRATES[@]}"; do
  echo ""
  echo "🔧 Building $crate..."

  if [ ! -f "$crate/Cargo.toml" ]; then
    echo "❌ Skipping $crate (no Cargo.toml)"
    continue
  fi

  # Create a temporary file to hold the current crate's compiler stderr
  TMP_ERR=$(mktemp)

  # Run maturin and route its error stream to the temp file
  if ! maturin develop -m "$crate/Cargo.toml" --release 2> "$TMP_ERR"; then
    echo "❌ Build failed for $crate!" >&2
    
    # 1. Format and write the error payload into the permanent log file
    echo "=== ERROR LOG FOR $crate ($(date)) ===" >> "$LOG_FILE"
    cat "$TMP_ERR" >> "$LOG_FILE"
    echo -e "\n==================================================\n" >> "$LOG_FILE"
    
    # 2. Flush the error to the console screen so you don't fly blind
    cat "$TMP_ERR" >&2
    rm -f "$TMP_ERR"
    
    echo "🛑 Compilation halted. Detailed errors saved to: ./$LOG_FILE" >&2
    exit 1
  fi

  # Clean up the temp file if the build succeeded
  rm -f "$TMP_ERR"
  echo "✅ Finished $crate"
done

echo ""
echo "🎉 All crates built successfully"