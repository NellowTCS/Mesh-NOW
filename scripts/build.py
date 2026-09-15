#!/usr/bin/env python3
"""
Mesh-NOW Target Selector and Builder
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
FIRMWARE_DIR = REPO_ROOT / "Firmware"


def check_idf_setup():
    """Check that the ESP-IDF environment is set up."""
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        print("Error: ESP-IDF environment not set up", file=sys.stderr)
        print("Run: source $ESP_IDF_PATH/export.sh", file=sys.stderr)
        sys.exit(1)
    return idf_path


def run_command(cmd, cwd=None):
    """Run a shell command and return whether it succeeded."""
    try:
        result = subprocess.run(
            cmd, shell=True, cwd=cwd, capture_output=True, text=True
        )
    except OSError as e:
        print(f"Error running command: {e}", file=sys.stderr)
        return False
    if result.returncode != 0:
        print(f"Command failed: {cmd}", file=sys.stderr)
        if result.stdout.strip():
            print(result.stdout, file=sys.stderr, end="")
        if result.stderr.strip():
            print(result.stderr, file=sys.stderr, end="")
        return False
    return True


def get_targets():
    """Return the available ESP32 targets with descriptions."""
    return {
        "esp32": "ESP32 DevKit C (Dual-core Xtensa, 520KB RAM)",
        "esp32s2": "ESP32-S2 Saola (Single-core Xtensa, 320KB RAM)",
        "esp32s3": "ESP32-S3 DevKitM (Dual-core Xtensa, 512KB RAM + PSRAM)",
        "esp32c3": "ESP32-C3 DevKitM (Single-core RISC-V, 400KB RAM)",
        "esp32c6": "ESP32-C6 DevKitC (Single-core RISC-V, 512KB RAM + 802.15.4)",
    }


def build_target(target):
    """Build for a target and copy the artifacts to Firmware/builds/<target>/."""
    build_dir = FIRMWARE_DIR / "build"
    if build_dir.exists():
        shutil.rmtree(build_dir)

    # Setting the target picks up sdkconfig.defaults.{target} automatically.
    print(f"Setting target to {target}...")
    if not run_command(f"idf.py set-target {target}"):
        print(f"Failed to set target {target}", file=sys.stderr)
        return False

    print(f"Building for {target}...")
    if not run_command("idf.py build"):
        print(f"Build failed for {target}", file=sys.stderr)
        return False

    builds_dir = FIRMWARE_DIR / "builds" / target
    builds_dir.mkdir(parents=True, exist_ok=True)

    artifacts = [
        ("mesh-now.bin", "build/mesh-now.bin"),
        ("bootloader.bin", "build/bootloader/bootloader.bin"),
        ("partition-table.bin", "build/partition_table/partition-table.bin"),
    ]
    for name, src in artifacts:
        src_path = FIRMWARE_DIR / src
        if src_path.exists():
            shutil.copy2(src_path, builds_dir / name)

    bin_file = builds_dir / "mesh-now.bin"
    if bin_file.exists():
        size_kb = bin_file.stat().st_size // 1024
        print(f"   Build successful! Binary size: {size_kb}KB")
        print(f"   Artifacts saved to: {builds_dir}")

    return True


def main():
    parser = argparse.ArgumentParser(description="Mesh-NOW Target Selector and Builder")
    parser.add_argument(
        "--target",
        type=str,
        default=None,
        help="Build a specific target (esp32, esp32s2, esp32s3, esp32c3, esp32c6)",
    )
    args = parser.parse_args()

    check_idf_setup()
    # idf.py build must run from the firmware project root.
    os.chdir(FIRMWARE_DIR)

    targets = get_targets()

    if args.target:
        if args.target not in targets:
            print(f"Invalid target: {args.target}", file=sys.stderr)
            sys.exit(1)
        if not build_target(args.target):
            sys.exit(1)
        sys.exit(0)

    failed = []
    for target in targets:
        print(f"Building {target}...")
        if not build_target(target):
            failed.append(target)

    if failed:
        print(f"Failed targets: {', '.join(failed)}", file=sys.stderr)
        sys.exit(1)

    print("All targets built successfully")


if __name__ == "__main__":
    main()
