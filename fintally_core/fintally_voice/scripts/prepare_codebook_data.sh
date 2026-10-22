#!/usr/bin/env bash

set -euo pipefail

echo "=== Fintally Voice - Codebook Data Preparation ==="
echo ""

# ==============================================================================
# 1. Create directories
# ==============================================================================

mkdir -p offline_builder/data_raw
mkdir -p offline_builder/data/en
mkdir -p offline_builder/data/fr
mkdir -p offline_builder/data/hi
mkdir -p offline_builder/data/zh-CN

# ==============================================================================
# 2. Explain dataset download
# ==============================================================================

./scripts/download_common_voice.sh

echo ""
echo "============================================================================"
echo "Dataset extraction required"
echo "============================================================================"
echo ""
echo "Extract the downloaded Common Voice archives into:"
echo ""
echo "    offline_builder/data_raw/"
echo ""
echo "Expected structure depends on the downloaded Common Voice release."
echo ""
read -r -p "Press Enter after extraction is complete..."

# ==============================================================================
# 3. Verify that data_raw is not empty
# ==============================================================================

if ! find offline_builder/data_raw -mindepth 1 -print -quit | grep -q .; then
    echo ""
    echo "ERROR: offline_builder/data_raw is empty."
    echo ""
    echo "Please download and extract the Common Voice datasets first."
    exit 1
fi

# ==============================================================================
# 4. Limit + convert
# ==============================================================================

echo ""
echo "=== Starting audio preparation ==="
echo ""

./scripts/limit_and_convert.sh

# ==============================================================================
# 5. Final verification
# ==============================================================================

echo ""
echo "=== Data preparation complete ==="
echo ""

for lang in en fr hi zh-CN; do

    dir="offline_builder/data/$lang"

    if [[ -d "$dir" ]]; then

        count="$(
            find "$dir" \
                -maxdepth 1 \
                -type f \
                -name '*.wav' \
                | wc -l
        )"

        echo "$lang: $count WAV files"

    else

        echo "$lang: directory missing"

    fi

done

echo ""
echo "You can now run the offline_builder to generate codebooks."
