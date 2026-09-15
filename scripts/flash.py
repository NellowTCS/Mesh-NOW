#!/usr/bin/env python3
"""
ESP32 Firmware Flasher
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path


def run_command(cmd, cwd=None):
    """Run a command and return (success, output)."""
    try:
        result = subprocess.run(
            cmd, shell=True, cwd=cwd, capture_output=True, text=True
        )
    except OSError as e:
        print(f"Error running command: {e}", file=sys.stderr)
        return False, str(e)
    if result.returncode != 0:
        print(f"Command failed: {cmd}", file=sys.stderr)
        if result.stderr.strip():
            print(result.stderr, file=sys.stderr, end="")
        return False, result.stderr
    return True, result.stdout


def bootloader_offset(target):
    return "0x1000" if target == "esp32" else "0x0"


def check_firmware_files():
    """Check that the required firmware binaries exist in the current directory."""
    required_files = ["mesh-now.bin", "bootloader.bin", "partition-table.bin"]
    missing_files = [file for file in required_files if not Path(file).exists()]
    if missing_files:
        print("Missing firmware files:", ", ".join(missing_files), file=sys.stderr)
        return False

    print("All firmware files found")
    return True


def get_file_size(file_path):
    """Return file size in human readable format."""
    size = Path(file_path).stat().st_size
    for unit in ['B', 'KB', 'MB', 'GB']:
        if size < 1024.0:
            return f"{size:.1f}{unit}"
        size /= 1024.0
    return f"{size:.1f}TB"


def flash_firmware(target, port):
    """Flash firmware to the ESP32 on the given port."""
    print(f"Flashing firmware to {target} on {port}")
    print("Make sure the node is in download mode (hold BOOT while pressing RESET)")
    print()

    print("Firmware files:")
    for file in ["bootloader.bin", "partition-table.bin", "mesh-now.bin"]:
        if Path(file).exists():
            print(f"  {file}: {get_file_size(file)}")
    print()

    success, _ = run_command("esptool.py --version")
    if not success:
        print("esptool.py not found. Installing...")
        success, _ = run_command(f"{sys.executable} -m pip install esptool")
        if not success:
            print("Failed to install esptool.py", file=sys.stderr)
            return False

    flash_cmd = (
        f"esptool.py --chip {target} --port {port} --baud 460800 "
        "--before default_reset --after hard_reset write_flash "
        "--flash_mode dio --flash_freq 40m --flash_size detect "
        f"{bootloader_offset(target)} bootloader.bin "
        "0x8000 partition-table.bin 0x10000 mesh-now.bin"
    )

    success, _ = run_command(flash_cmd)
    if not success:
        print("Flash failed!", file=sys.stderr)
        return False

    print("Flash complete!")
    print()
    print("Next steps:")
    print("  1. Reset the node (press RESET button)")
    print("  2. Run the web UI: cd Demo && npm run serve")
    print("  3. Connect the node via Web Serial")
    return True


def main():
    parser = argparse.ArgumentParser(description="Flash ESP32 firmware")
    parser.add_argument(
        "target",
        nargs="?",
        default="esp32",
        help="ESP32 target (esp32, esp32s2, esp32s3, esp32c3, esp32c6)",
    )
    parser.add_argument(
        "port",
        nargs="?",
        default="/dev/ttyUSB0" if os.name != 'nt' else "COM1",
        help="Serial port",
    )
    args = parser.parse_args()

    if not check_firmware_files():
        print("Cannot proceed without firmware files", file=sys.stderr)
        sys.exit(1)

    if not flash_firmware(args.target, args.port):
        sys.exit(1)


if __name__ == "__main__":
    main()
