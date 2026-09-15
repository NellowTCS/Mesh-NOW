#!/usr/bin/env python3
"""
Mesh-NOW Multi-Target Build Script
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


def run_command(cmd, cwd=None, env=None):
    """Run a shell command and return whether it succeeded."""
    try:
        result = subprocess.run(
            cmd, shell=True, cwd=cwd, env=env, capture_output=True, text=True
        )
    except OSError as e:
        print(f"Error running command: {e}", file=sys.stderr)
        return False
    if result.returncode != 0:
        print(f"Command failed: {cmd}", file=sys.stderr)
        if result.stderr.strip():
            print(result.stderr, file=sys.stderr, end="")
        return False
    return True


def get_targets():
    """Return the available ESP32 targets."""
    return ["esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"]


def build_target(target):
    """Build for a target and copy the artifacts to Firmware/builds/<target>/."""
    # espressif/esp-idf-ci-action exports IDF_TARGET for the first target
    env = os.environ.copy()
    env["IDF_TARGET"] = target

    build_dir = FIRMWARE_DIR / "build"
    if build_dir.exists():
        shutil.rmtree(build_dir)

    for sdkconfig in FIRMWARE_DIR.glob("sdkconfig*"):
        if sdkconfig.name.startswith("sdkconfig.defaults"):
            continue
        if sdkconfig.is_file():
            sdkconfig.unlink()

    # Setting the target picks up sdkconfig.defaults.{target} automatically.
    print(f"Setting target to {target}...")
    if not run_command(f"idf.py set-target {target}", env=env):
        print(f"Failed to set target {target}", file=sys.stderr)
        return False

    print(f"Building for {target}...")
    if not run_command("idf.py build", env=env):
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
    parser = argparse.ArgumentParser(description="Mesh-NOW Multi-Target Build Script")
    parser.add_argument(
        "--targets", nargs="*", help="Build specific targets (default: all)"
    )
    args = parser.parse_args()

    check_idf_setup()
    # idf.py build must run from the firmware project root.
    os.chdir(FIRMWARE_DIR)

    targets = args.targets if args.targets else get_targets()

    print("Mesh-NOW Multi-Target Build Script")
    print(f"Targets: {', '.join(targets)}")
    print()

    failed_builds = []
    successful_builds = []
    for target in targets:
        print(f"Building for {target}...")
        if build_target(target):
            successful_builds.append(target)
        else:
            failed_builds.append(target)
            print(f"{target}: FAILED", file=sys.stderr)
        print()

    print("=" * 40)
    print("Build Summary:")
    print("=" * 40)
    print(
        f"Successful builds ({len(successful_builds)}): {', '.join(successful_builds)}"
    )
    if failed_builds:
        print(f"Failed builds ({len(failed_builds)}): {', '.join(failed_builds)}")
    print()

    print("Build artifacts location:")
    print("   builds/")
    for target in successful_builds:
        print(f"   {target}/")
        for artifact in ["mesh-now.bin", "bootloader.bin", "partition-table.bin"]:
            print(f"      {artifact}")

    print()
    print("Flash commands:")
    for target in successful_builds:
        print(f"   {target}: idf.py set-target {target} && idf.py flash")

    if failed_builds:
        print()
        print(
            "Some builds failed. Check the output above for details.", file=sys.stderr
        )
        sys.exit(1)

    print()
    print("All builds completed successfully!")


if __name__ == "__main__":
    main()
