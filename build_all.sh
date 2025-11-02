#!/bin/bash

# Zephyr-NOW Build Script for All ESP Targets
# This script builds the project for all supported ESP32 targets

set -e

echo "=========================================="
echo "Zephyr-NOW Multi-Target Build Script"
echo "=========================================="

# Check if west is installed
if ! command -v west &> /dev/null; then
    echo "Error: west is not installed"
    echo "Install with: pip install west"
    exit 1
fi

# Initialize west if not already done
if [ ! -d "zephyr" ]; then
    echo "Initializing west workspace..."
    west init -l .
    west update
fi

# Set Zephyr base
export ZEPHYR_BASE=$(pwd)/zephyr

# Array of supported boards
declare -a boards=(
    "esp32_devkitc_wroom"
    "esp32s2_saola"
    "esp32s3_devkitm"
    "esp32c3_devkitm"
)

# Array of board configs (optional, if they exist)
declare -a configs=(
    "esp32.conf"
    "esp32s2.conf"
    "esp32s3.conf"
    "esp32c3.conf"
)

echo ""
echo "Building for the following targets:"
for board in "${boards[@]}"; do
    echo "  - $board"
done
echo ""

# Build each target
for i in "${!boards[@]}"; do
    board="${boards[$i]}"
    conf="${configs[$i]}"
    
    echo "=========================================="
    echo "Building for: $board"
    echo "=========================================="
    
    # Build command
    if [ -f "$conf" ]; then
        west build -b "$board" -d "build_$board" -- -DCONF_FILE="prj.conf;$conf"
    else
        west build -b "$board" -d "build_$board"
    fi
    
    if [ $? -eq 0 ]; then
        echo "✓ Build successful for $board"
    else
        echo "✗ Build failed for $board"
        exit 1
    fi
    echo ""
done

echo "=========================================="
echo "All builds completed successfully!"
echo "=========================================="
echo ""
echo "Firmware locations:"
for board in "${boards[@]}"; do
    echo "  $board: build_$board/zephyr/zephyr.bin"
done
echo ""
echo "To flash a specific target:"
echo "  west flash -d build_<board_name>"
