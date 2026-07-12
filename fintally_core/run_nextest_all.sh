#!/usr/bin/env bash

# Define log files
FULL_LOG="nextest_full.log"
FAILURE_LOG="failed_tests.log"

# Clean up previous run logs
rm -f "$FULL_LOG" "$FAILURE_LOG"

clear
echo "================================================================="
echo "⚡       FINTALLY WORKSPACE: CARGO NEXTEST ENGINE RUNNER        "
echo "================================================================="
echo "Workspace: $(pwd)"
echo "-----------------------------------------------------------------"

# Allow the script to keep running even if tests fail so we can process logs
set +e

# Run nextest across the whole workspace:
# --workspace: Runs all tests in rust_backend, fintally_chatbot, fintally_finance
# --failure-output final: Forces nextest to print failure details AT THE END
# tee: Streams live to your console while saving everything to a full log file
cargo nextest run --workspace --failure-output final 2>&1 | tee "$FULL_LOG"

# Capture the exit code of the cargo nextest command (the first command in the pipe)
NEXTEST_EXIT_CODE=${PIPESTATUS[0]}

echo "-----------------------------------------------------------------"
echo "📊 LOG SYSTEM: AGGREGATING RESULTS..."
echo "-----------------------------------------------------------------"

if [ $NEXTEST_EXIT_CODE -ne 0 ]; then
    echo "❌ Failures detected. Writing failure log to: ./$FAILURE_LOG"
    
    echo "=====================================================" > "$FAILURE_LOG"
    echo "🚨 FINTALLY TEST FAILURES - RETRIEVED AT LAST       " >> "$FAILURE_LOG"
    echo "Generated on: $(date)" >> "$FAILURE_LOG"
    echo "=====================================================" >> "$FAILURE_LOG"
    
    # Extract the beautiful failure summary block that Nextest prints at the end
    if grep -q "Failure summary" "$FULL_LOG"; then
        sed -n '/Failure summary/,$p' "$FULL_LOG" >> "$FAILURE_LOG"
    else
        # Fallback if the string formatting differs
        grep -iE "FAIL|failed|error" "$FULL_LOG" >> "$FAILURE_LOG"
    fi
    
    echo -e "\n📌 --- PRINTING EXTRACTED FAILURES AT LAST ---"
    cat "$FAILURE_LOG"
    echo "-----------------------------------------------------------------"
    
    # Exit with the original failure code so CI/CD or your terminal knows it failed
    exit $NEXTEST_EXIT_CODE
else
    echo "✅ SUCCESS: All tests passed across all workspace crates!"
    exit 0
fi