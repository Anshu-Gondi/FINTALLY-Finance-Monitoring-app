#!/usr/bin/env bash

set -euo pipefail

# ==============================================================================
# limit_and_convert.sh
#
# Purpose:
#   Prepare a deterministic speech dataset for the Fintally Voice offline
#   codebook builder.
#
#   Each language is limited to at most HOURS_PER_LANG of ACTUAL audio duration.
#
#   Output format:
#       WAV
#       16 kHz
#       mono
#       signed 16-bit PCM
#
# Dataset formats:
#
#   English / French / Chinese:
#       SPS Corpus
#       TSV column:
#           audio_file
#           duration_ms
#
#   Hindi:
#       Common Voice
#       TSV column:
#           path
#       Duration source:
#           clip_durations.tsv
#
# ==============================================================================


# ==============================================================================
# CONFIGURATION
# ==============================================================================

HOURS_PER_LANG=10.0

TARGET_SR=16000
TARGET_CHANNELS=1
TARGET_SAMPLE_FMT="s16"

RAW_DIR="./offline_builder/data_raw"
OUT_DIR="./offline_builder/data"

TARGET_DURATION_MS="$(
    awk -v hours="$HOURS_PER_LANG" '
        BEGIN {
            printf "%.0f", hours * 60 * 60 * 1000
        }
    '
)"


# ==============================================================================
# REQUIREMENTS
# ==============================================================================

require_command() {
    local command_name="$1"

    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "ERROR: Required command not found: $command_name"
        exit 1
    fi
}

require_command ffmpeg
require_command awk
require_command find
require_command tail


# ==============================================================================
# SETUP
# ==============================================================================

mkdir -p "$OUT_DIR"

echo "============================================================"
echo " Fintally Voice - Audio Dataset Preparation"
echo "============================================================"
echo "Target duration : ${HOURS_PER_LANG} hours / language"
echo "Target sample rate: ${TARGET_SR} Hz"
echo "Target channels : ${TARGET_CHANNELS}"
echo "Target format   : signed 16-bit PCM WAV"
echo "Output          : ${OUT_DIR}"
echo "============================================================"
echo ""


# ==============================================================================
# FORMAT MILLISECONDS AS HH:MM:SS
# ==============================================================================

format_duration() {
    local duration_ms="$1"

    awk -v ms="$duration_ms" '
        BEGIN {
            total_seconds = int(ms / 1000)

            hours = int(total_seconds / 3600)
            minutes = int((total_seconds % 3600) / 60)
            seconds = total_seconds % 60

            printf "%02d:%02d:%02d", hours, minutes, seconds
        }
    '
}


# ==============================================================================
# PROCESS SPS DATASET
#
# Arguments:
#
#   1 = language
#   2 = audio directory
#   3 = metadata TSV
#
# SPS format:
#
#   client_id
#   audio_id
#   audio_file
#   duration_ms
#   ...
#
# zero-based:
#
#   audio_file  = column 2
#   duration_ms = column 3
#
# ==============================================================================

process_sps_language() {

    local lang="$1"
    local clips_dir="$2"
    local tsv="$3"

    echo "============================================================"
    echo "Processing SPS language: $lang"
    echo "============================================================"
    echo "Audio directory : $clips_dir"
    echo "Metadata        : $tsv"
    echo ""

    local lang_out="$OUT_DIR/$lang"

    mkdir -p "$lang_out"

    # Remove only generated WAV files.
    find "$lang_out" \
        -maxdepth 1 \
        -type f \
        -name '*.wav' \
        -delete

    # --------------------------------------------------------------------------
    # Validate inputs
    # --------------------------------------------------------------------------

    if [[ ! -d "$clips_dir" ]]; then
        echo "ERROR: Audio directory does not exist:"
        echo "       $clips_dir"
        return 1
    fi

    if [[ ! -f "$tsv" ]]; then
        echo "ERROR: Metadata TSV does not exist:"
        echo "       $tsv"
        return 1
    fi

    # --------------------------------------------------------------------------
    # Counters
    # --------------------------------------------------------------------------

    local processed=0
    local missing=0
    local failed=0

    local accumulated_ms=0

    # --------------------------------------------------------------------------
    # Read metadata.
    #
    # Column 2 = audio_file
    # Column 3 = duration_ms
    # --------------------------------------------------------------------------

    while IFS=$'\t' read -r \
        client_id \
        audio_id \
        audio_file \
        duration_ms \
        rest
    do

        # Remove CR from Windows-style TSV files.
        audio_file="${audio_file//$'\r'/}"
        duration_ms="${duration_ms//$'\r'/}"

        # Skip malformed rows.
        if [[ -z "$audio_file" || -z "$duration_ms" ]]; then
            continue
        fi

        # Validate duration.
        if [[ ! "$duration_ms" =~ ^[0-9]+$ ]]; then
            continue
        fi

        # Stop before exceeding target duration.
        if (( accumulated_ms + duration_ms > TARGET_DURATION_MS )); then
            break
        fi

        local filename
        filename="$(basename "$audio_file")"

        local source="$clips_dir/$filename"

        if [[ ! -f "$source" ]]; then
            echo "!! Missing: $source"
            ((missing += 1))
            continue
        fi

        local stem="${filename%.*}"
        local output="$lang_out/${stem}.wav"

        # ----------------------------------------------------------------------
        # Convert
        # ----------------------------------------------------------------------

        if ffmpeg \
            -y \
            -loglevel error \
            -i "$source" \
            -ar "$TARGET_SR" \
            -ac "$TARGET_CHANNELS" \
            -sample_fmt "$TARGET_SAMPLE_FMT" \
            "$output"
        then

            ((processed += 1))
            ((accumulated_ms += duration_ms))

        else

            echo "!! ffmpeg failed:"
            echo "   $source"

            rm -f "$output"

            ((failed += 1))

        fi

    done < <(tail -n +2 "$tsv")

    # --------------------------------------------------------------------------
    # Final statistics
    # --------------------------------------------------------------------------

    local final_count

    final_count="$(
        find "$lang_out" \
            -maxdepth 1 \
            -type f \
            -name '*.wav' \
            | wc -l
    )"

    echo ""
    echo "Language        : $lang"
    echo "WAV files       : $final_count"
    echo "Processed       : $processed"
    echo "Missing         : $missing"
    echo "Failed          : $failed"
    echo "Selected audio  : $(format_duration "$accumulated_ms")"
    echo "Target          : $(format_duration "$TARGET_DURATION_MS")"
    echo "Output          : $lang_out"
    echo ""

    if (( accumulated_ms >= TARGET_DURATION_MS )); then
        echo "Status          : TARGET REACHED"
    else
        echo "Status          : DATA EXHAUSTED BEFORE TARGET"
    fi

    echo ""
}


# ==============================================================================
# PROCESS HINDI COMMON VOICE
#
# Arguments:
#
#   1 = language
#   2 = clips directory
#   3 = validated.tsv
#   4 = clip_durations.tsv
#
# validated.tsv:
#
#   column 1 = client_id
#   column 2 = path
#
# clip_durations.tsv:
#
#   column 1 = clip
#   column 2 = duration[ms]
#
# ==============================================================================

process_hindi() {

    local lang="$1"
    local clips_dir="$2"
    local tsv="$3"
    local durations_tsv="$4"

    echo "============================================================"
    echo "Processing Common Voice language: $lang"
    echo "============================================================"
    echo "Audio directory : $clips_dir"
    echo "Metadata        : $tsv"
    echo "Durations       : $durations_tsv"
    echo ""

    local lang_out="$OUT_DIR/$lang"

    mkdir -p "$lang_out"

    # Remove previous generated WAV files.
    find "$lang_out" \
        -maxdepth 1 \
        -type f \
        -name '*.wav' \
        -delete

    # --------------------------------------------------------------------------
    # Validate inputs
    # --------------------------------------------------------------------------

    if [[ ! -d "$clips_dir" ]]; then
        echo "ERROR: Hindi clips directory does not exist:"
        echo "       $clips_dir"
        return 1
    fi

    if [[ ! -f "$tsv" ]]; then
        echo "ERROR: Hindi validated.tsv does not exist:"
        echo "       $tsv"
        return 1
    fi

    if [[ ! -f "$durations_tsv" ]]; then
        echo "ERROR: Hindi clip_durations.tsv does not exist:"
        echo "       $durations_tsv"
        return 1
    fi

    # --------------------------------------------------------------------------
    # Load duration metadata into an associative array.
    # --------------------------------------------------------------------------

    declare -A durations

    while IFS=$'\t' read -r clip duration_ms; do

        clip="${clip//$'\r'/}"
        duration_ms="${duration_ms//$'\r'/}"

        # Skip header.
        if [[ "$clip" == "clip" ]]; then
            continue
        fi

        if [[ -z "$clip" || ! "$duration_ms" =~ ^[0-9]+$ ]]; then
            continue
        fi

        durations["$clip"]="$duration_ms"

    done < "$durations_tsv"

    echo "Loaded ${#durations[@]} duration entries."
    echo ""

    # --------------------------------------------------------------------------
    # Counters
    # --------------------------------------------------------------------------

    local processed=0
    local missing=0
    local no_duration=0
    local failed=0

    local accumulated_ms=0

    # --------------------------------------------------------------------------
    # Read validated.tsv
    #
    # Column 1 = client_id
    # Column 2 = path
    # --------------------------------------------------------------------------

    while IFS=$'\t' read -r \
        client_id \
        filename \
        rest
    do

        filename="${filename//$'\r'/}"

        # Skip malformed rows.
        if [[ -z "$filename" ]]; then
            continue
        fi

        # ----------------------------------------------------------------------
        # Get duration.
        # ----------------------------------------------------------------------

        local duration_ms="${durations[$filename]:-}"

        if [[ -z "$duration_ms" ]]; then
            echo "!! No duration metadata: $filename"
            ((no_duration += 1))
            continue
        fi

        # Stop before exceeding target.
        if (( accumulated_ms + duration_ms > TARGET_DURATION_MS )); then
            break
        fi

        local source="$clips_dir/$filename"

        if [[ ! -f "$source" ]]; then
            echo "!! Missing: $source"
            ((missing += 1))
            continue
        fi

        local stem="${filename%.*}"
        local output="$lang_out/${stem}.wav"

        # ----------------------------------------------------------------------
        # Convert
        # ----------------------------------------------------------------------

        if ffmpeg \
            -y \
            -loglevel error \
            -i "$source" \
            -ar "$TARGET_SR" \
            -ac "$TARGET_CHANNELS" \
            -sample_fmt "$TARGET_SAMPLE_FMT" \
            "$output"
        then

            ((processed += 1))
            ((accumulated_ms += duration_ms))

        else

            echo "!! ffmpeg failed:"
            echo "   $source"

            rm -f "$output"

            ((failed += 1))

        fi

    done < <(tail -n +2 "$tsv")

    # --------------------------------------------------------------------------
    # Final statistics
    # --------------------------------------------------------------------------

    local final_count

    final_count="$(
        find "$lang_out" \
            -maxdepth 1 \
            -type f \
            -name '*.wav' \
            | wc -l
    )"

    echo ""
    echo "Language        : $lang"
    echo "WAV files       : $final_count"
    echo "Processed       : $processed"
    echo "Missing         : $missing"
    echo "No duration     : $no_duration"
    echo "Failed          : $failed"
    echo "Selected audio  : $(format_duration "$accumulated_ms")"
    echo "Target          : $(format_duration "$TARGET_DURATION_MS")"
    echo "Output          : $lang_out"
    echo ""

    if (( accumulated_ms >= TARGET_DURATION_MS )); then
        echo "Status          : TARGET REACHED"
    else
        echo "Status          : DATA EXHAUSTED BEFORE TARGET"
    fi

    echo ""
}


# ==============================================================================
# ENGLISH
# ==============================================================================

process_sps_language \
    "en" \
    "$RAW_DIR/sps-corpus-5.0-2026-09-11-en/audios" \
    "$RAW_DIR/sps-corpus-5.0-2026-09-11-en/ss-corpus-en.tsv"


# ==============================================================================
# FRENCH
# ==============================================================================

process_sps_language \
    "fr" \
    "$RAW_DIR/sps-corpus-5.0-2026-09-11-fr/audios" \
    "$RAW_DIR/sps-corpus-5.0-2026-09-11-fr/ss-corpus-fr.tsv"


# ==============================================================================
# HINDI
# ==============================================================================

process_hindi \
    "hi" \
    "$RAW_DIR/cv-corpus-27.0-2026-09-11/hi/clips" \
    "$RAW_DIR/cv-corpus-27.0-2026-09-11/hi/validated.tsv" \
    "$RAW_DIR/cv-corpus-27.0-2026-09-11/hi/clip_durations.tsv"


# ==============================================================================
# CHINESE
# ==============================================================================

process_sps_language \
    "zh-CN" \
    "$RAW_DIR/sps-corpus-5.0-2026-09-11-zh-CN/audios" \
    "$RAW_DIR/sps-corpus-5.0-2026-09-11-zh-CN/ss-corpus-zh-CN.tsv"


# ==============================================================================
# FINAL SUMMARY
# ==============================================================================

echo ""
echo "============================================================"
echo " FINAL DATASET SUMMARY"
echo "============================================================"

for lang in en fr hi zh-CN; do

    lang_dir="$OUT_DIR/$lang"

    if [[ -d "$lang_dir" ]]; then

        count="$(
            find "$lang_dir" \
                -maxdepth 1 \
                -type f \
                -name '*.wav' \
                | wc -l
        )"

        echo "$lang : $count WAV files"

    else

        echo "$lang : output directory missing"

    fi

done

echo ""
echo "============================================================"
echo " Done"
echo "============================================================"
