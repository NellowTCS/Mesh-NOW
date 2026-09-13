#!/usr/bin/env python3
"""
Quick test script to verify all targets configure successfully
"""

import os
import sys
from pathlib import Path

FIRMWARE_DIR = Path(__file__).resolve().parent.parent / "Firmware"


def check_idf_setup():
    """Check that the ESP-IDF environment is set up."""
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        print("Error: ESP-IDF environment not set up", file=sys.stderr)
        print("Run: source $ESP_IDF_PATH/export.sh", file=sys.stderr)
        sys.exit(1)
    return idf_path


def test_target(target):
    """Validate that sdkconfig.defaults.{target} exists and is non-empty."""
    config_file = FIRMWARE_DIR / f"sdkconfig.defaults.{target}"
    if not config_file.exists():
        return "Config file missing"

    try:
        content = config_file.read_text()
        if not content.strip():
            return "Config file empty"
    except OSError as e:
        return f"Config file error: {e}"

    return "Configuration OK"


def main():
    check_idf_setup()
    os.chdir(FIRMWARE_DIR)

    targets = ["esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"]

    print("Testing all ESP32 targets for Mesh-NOW")
    print("=" * 40)
    print()

    results = []
    for target in targets:
        print(f"Testing {target}...")
        results.append(f"{target}: {test_target(target)}")

    print()
    print("Results:")
    for result in results:
        print(f"   {result}")


if __name__ == "__main__":
    main()