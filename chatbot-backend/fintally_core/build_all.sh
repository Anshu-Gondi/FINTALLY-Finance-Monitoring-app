#!/usr/bin/env bash
set -e  # stop on error

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

  maturin develop -m "$crate/Cargo.toml" --release

  echo "✅ Finished $crate"
done

echo ""
echo "🎉 All crates built successfully"