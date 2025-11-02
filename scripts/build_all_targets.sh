#!/bin/bash

# Mesh-NOW Multi-Target Build Script
# Builds for all supported ESP32 variants

set -e  # Exit on any error

echo "Mesh-NOW Multi-Target Build Script"
echo "======================================"

# Check if ESP-IDF is set up
if [ -z "$IDF_PATH" ]; then
    echo "Error: ESP-IDF environment not set up"
    echo "Please run: source \$ESP_IDF_PATH/export.sh"
    exit 1
fi

# Build targets array
TARGETS=("esp32" "esp32s2" "esp32s3" "esp32c3" "esp32c6")
BUILD_DIR="build"
FAILED_BUILDS=()

echo "Available targets: ${TARGETS[*]}"
echo ""

# Function to build for a specific target
build_target() {
    local target=$1
    echo "Building for $target..."
    
    # Clean previous build
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
    fi
    
    # Set target
    idf.py set-target "$target" || {
        echo "Failed to set target $target"
        return 1
    }
    
    # Copy target-specific config if it exists
    if [ -f "configs/sdkconfig.$target" ]; then
        echo "Using configuration: configs/sdkconfig.$target"
        cp "configs/sdkconfig.$target" sdkconfig.defaults
    fi
    
    # Build
    idf.py build || {
        echo "Build failed for $target"
        return 1
    }
    
    # Create target-specific build directory
    mkdir -p "builds/$target"
    
    # Copy important build artifacts
    if [ -f "$BUILD_DIR/mesh-now.bin" ]; then
        cp "$BUILD_DIR/mesh-now.bin" "builds/$target/"
        cp "$BUILD_DIR/bootloader/bootloader.bin" "builds/$target/"
        cp "$BUILD_DIR/partition_table/partition-table.bin" "builds/$target/"
        
        # Get binary size
        local size=$(stat -c%s "$BUILD_DIR/mesh-now.bin")
        local size_kb=$((size / 1024))
        echo "   Build successful! Binary size: ${size_kb}KB"
        echo "   Artifacts saved to: builds/$target/"
    else
        echo "Build artifacts not found for $target"
        return 1
    fi
    
    return 0
}

# Main build loop
echo "Starting multi-target build..."
echo ""

for target in "${TARGETS[@]}"; do
    echo "----------------------------------------"
    if build_target "$target"; then
        echo "$target: SUCCESS"
    else
        echo "$target: FAILED"
        FAILED_BUILDS+=("$target")
    fi
    echo ""
done

# Summary
echo "========================================="
echo "Build Summary:"
echo "========================================="

successful_builds=()
for target in "${TARGETS[@]}"; do
    if [[ ! " ${FAILED_BUILDS[*]} " =~ " ${target} " ]]; then
        successful_builds+=("$target")
    fi
done

echo "Successful builds (${#successful_builds[@]}): ${successful_builds[*]}"
if [ ${#FAILED_BUILDS[@]} -gt 0 ]; then
    echo "Failed builds (${#FAILED_BUILDS[@]}): ${FAILED_BUILDS[*]}"
fi

echo ""
echo "Build artifacts location:"
echo "   builds/"
for target in "${successful_builds[@]}"; do
    echo "   ├── $target/"
    echo "   │   ├── mesh-now.bin"
    echo "   │   ├── bootloader.bin"
    echo "   │   └── partition-table.bin"
done

echo ""
echo "Flash commands:"
for target in "${successful_builds[@]}"; do
    echo "   $target: idf.py set-target $target && idf.py flash"
done

if [ ${#FAILED_BUILDS[@]} -eq 0 ]; then
    echo ""
    echo "All builds completed successfully!"
    exit 0
else
    echo ""
    echo "Some builds failed. Check the output above for details."
    exit 1
fi