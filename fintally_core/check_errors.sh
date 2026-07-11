#!/usr/bin/env bash

# Clear any previous log file
rm -f errors.log

echo "Running package check in release mode..."
echo "Command: cargo check -p fintally_axum --release"
echo "--------------------------------------------------"

# Capture both standard output and standard errors into errors.log
cargo check -p fintally_axum --release > errors.log 2>&1

echo "Done! Opening errors.log in VS Code..."

# Opens the generated file as a new tab in your VS Code editor
code errors.log