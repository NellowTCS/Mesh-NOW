#!/bin/bash

# Mesh-NOW Target Selector and Builder
# Interactive script to select and build for specific ESP32 targets

set -e

echo "Mesh-NOW Target Selector"
echo "=========================="

# Check if ESP-IDF is set up
if [ -z "$IDF_PATH" ]; then
    echo "Error: ESP-IDF environment not set up"
    echo "Please run: source \$ESP_IDF_PATH/export.sh"
    exit 1
fi

# Available targets with descriptions
declare -A TARGETS
TARGETS[1]="esp32:ESP32 DevKit C (Dual-core Xtensa, 520KB RAM)"
TARGETS[2]="esp32s2:ESP32-S2 Saola (Single-core Xtensa, 320KB RAM)"  
TARGETS[3]="esp32s3:ESP32-S3 DevKitM (Dual-core Xtensa, 512KB RAM + PSRAM)"
TARGETS[4]="esp32c3:ESP32-C3 DevKitM (Single-core RISC-V, 400KB RAM)"
TARGETS[5]="esp32c6:ESP32-C6 DevKitC (Single-core RISC-V, 512KB RAM + 802.15.4)"

echo "Available targets:"
echo ""
for i in {1..5}; do
    IFS=':' read -r target desc <<< "${TARGETS[$i]}"
    echo "$i) $target - $desc"
done
echo "6) Build all targets"
echo "0) Exit"
echo ""

# Get user choice
while true; do
    read -p "Select target (0-6): " choice
    case $choice in
        [1-5])
            IFS=':' read -r target desc <<< "${TARGETS[$choice]}"
            echo ""
            echo "Selected: $target - $desc"
            
            # Set target
            echo "Setting target to $target..."
            idf.py set-target "$target"
            
            # Apply target-specific config
            if [ -f "configs/sdkconfig.$target" ]; then
                echo "Applying configuration: configs/sdkconfig.$target"
                cp "configs/sdkconfig.$target" sdkconfig.defaults
            fi
            
            # Build
            echo "Building for $target..."
            idf.py build
            
            echo ""
            echo "Build completed for $target!"
            echo "To flash: idf.py flash monitor"
            break
            ;;
        6)
            echo ""
            echo "Building all targets..."
            ./build_all_targets.sh
            break
            ;;
        0)
            echo "Goodbye!"
            exit 0
            ;;
        *)
            echo "Invalid choice. Please select 0-6."
            ;;
    esac
done