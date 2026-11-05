#!/bin/bash
set -e

# ========== Configuration ==========
LANGUAGES=("en" "fr" "hi" "zh-CN")
DATA_ROOT="./offline_builder/data_raw"
mkdir -p "$DATA_ROOT"

echo "=== Downloading Common Voice datasets ==="
echo "Note: Full datasets are large. We will limit them later."

for lang in "${LANGUAGES[@]}"; do
    echo ""
    echo "→ Language: $lang"

    # Official page (manual download is currently the most reliable)
    echo "Please download the latest dataset for '$lang' from:"
    echo "https://commonvoice.mozilla.org/en/datasets"
    echo "Save the .tar.gz file into: $DATA_ROOT/"
    echo ""
done

echo "After downloading the .tar.gz files, run the next script."
