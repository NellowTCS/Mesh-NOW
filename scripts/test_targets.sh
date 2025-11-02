#!/bin/bash

# Quick test script to verify all targets configure successfully
echo "Testing all ESP32 targets for Mesh-NOW"
echo "=========================================="

cd /workspaces/Zephyr-NOW

TARGETS=("esp32" "esp32s2" "esp32s3" "esp32c3" "esp32c6")
RESULTS=()

for target in "${TARGETS[@]}"; do
    echo "Testing $target..."
    rm -rf build >/dev/null 2>&1
    
    # Set target and use target-specific config
    if idf.py set-target "$target" >/dev/null 2>&1; then
        if [ -f "configs/sdkconfig.$target" ]; then
            cp "configs/sdkconfig.$target" sdkconfig.defaults
        fi
        
        # Try to build (just configuration phase)
        if idf.py reconfigure >/dev/null 2>&1; then
            RESULTS+=("$target: Configuration OK")
        else
            RESULTS+=("$target: Configuration failed")
        fi
    else
        RESULTS+=("$target: Target setup failed")
    fi
done

echo ""
echo "Results:"
for result in "${RESULTS[@]}"; do
    echo "   $result"
done