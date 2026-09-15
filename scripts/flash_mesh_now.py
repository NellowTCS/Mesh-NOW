#!/usr/bin/env python3
"""
Universal Mesh-NOW Flash Tool
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


def list_serial_ports():
    """Return serial ports, using pyserial when available."""
    try:
        from serial.tools import list_ports

        return [port.device for port in list_ports.comports()]
    except ImportError:
        return []


def select_port():
    """Return a serial port, defaulting to a platform convention if none detected."""
    ports = list_serial_ports()
    if ports:
        print(f"Detected serial port: {ports[0]}")
        return ports[0]

    if os.name == "nt":
        return "COM1"
    return "/dev/ttyUSB0"


def find_firmware_dirs(search_path=None):
    """Find directories containing built firmware binaries."""
    if search_path is None:
        search_paths = [
            Path("../release/firmware"),
            Path("./firmware"),
            Path("./release/firmware"),
        ]
    else:
        search_paths = [Path(search_path)]

    firmware_dirs = {}
    for path in search_paths:
        if path.exists() and path.is_dir():
            for item in path.iterdir():
                if item.is_dir() and (item / "mesh-now.bin").exists():
                    firmware_dirs[item.name] = item

    return firmware_dirs


def flash_target(target_name, target_path, port):
    """Flash firmware for the selected target."""
    print(f"Flashing {target_name} firmware...")
    print("Make sure the node is in download mode (hold BOOT while pressing RESET)")
    print()

    original_cwd = Path.cwd()
    try:
        os.chdir(target_path)

        flash_script = target_path / "flash.py"
        if flash_script.exists():
            print("Using target-specific flash script")
            cmd = f"{sys.executable} flash.py {target_name} {port}"
        else:
            print("Using manual flash command")
            bl_offset = "0x1000" if target_name == "esp32" else "0x0"
            cmd = (
                f"esptool.py --chip {target_name} --port {port} --baud 460800 "
                "--before default_reset --after hard_reset write_flash "
                "--flash_mode dio --flash_freq 40m --flash_size detect "
                f"{bl_offset} bootloader.bin "
                "0x8000 partition-table.bin 0x10000 mesh-now.bin"
            )

        success, _ = run_command(cmd)

        if not success:
            print(f"Failed to flash {target_name}", file=sys.stderr)
            return False

        print("Flash complete!")
        print()
        print("Next steps:")
        print("  1. Reset the node (press RESET button)")
        print("  2. Run the web UI: cd Demo && npm run serve")
        print("  3. Connect the node via Web Serial")
        return True
    finally:
        os.chdir(original_cwd)


def main():
    parser = argparse.ArgumentParser(description="Universal Mesh-NOW flash tool")
    parser.add_argument("firmware_dir", nargs="?", help="Path to firmware directory")
    parser.add_argument(
        "--port", default=None, help="Serial port (auto-detected if omitted)"
    )
    args = parser.parse_args()

    firmware_dirs = find_firmware_dirs(args.firmware_dir)

    if not firmware_dirs:
        print("No firmware directories found!", file=sys.stderr)
        print("Searched in:")
        print("  - ../release/firmware")
        print("  - ./firmware")
        print("  - ./release/firmware")
        print()
        print("Use: python flash_mesh_now.py /path/to/firmware")
        sys.exit(1)

    print(f"Found {len(firmware_dirs)} firmware target(s)")
    for i, (target, path) in enumerate(firmware_dirs.items(), 1):
        print(f"  {i}. {target}")

    if len(firmware_dirs) == 1:
        target_name, target_path = list(firmware_dirs.items())[0]
        print(f"Using only available target: {target_name}")
    else:
        print(
            "Multiple targets found. Specify the firmware directory explicitly.",
            file=sys.stderr,
        )
        sys.exit(1)

    port = args.port or select_port()
    print(f"Selected port: {port}")
    print()

    if not flash_target(target_name, target_path, port):
        sys.exit(1)


if __name__ == "__main__":
    main()
