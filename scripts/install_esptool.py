#!/usr/bin/env python3
"""
ESP32 Tool Installer
"""

import argparse
import subprocess
import sys


def run_command(cmd):
    """Run a command and return (success, output)."""
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    except OSError as e:
        print(f"Error running command: {e}", file=sys.stderr)
        return False, str(e)
    if result.returncode != 0:
        print(f"Command failed: {cmd}", file=sys.stderr)
        if result.stderr.strip():
            print(result.stderr, file=sys.stderr, end="")
        return False, result.stderr
    return True, result.stdout


def check_python():
    """Check that Python is available."""
    try:
        result = subprocess.run(
            [sys.executable, "--version"], capture_output=True, text=True
        )
        return result.returncode == 0, result.stdout.strip()
    except OSError:
        return False, ""


def check_pip():
    """Check that pip is available."""
    try:
        result = subprocess.run(
            [sys.executable, "-m", "pip", "--version"], capture_output=True, text=True
        )
        return result.returncode == 0, result.stdout.strip()
    except OSError:
        return False, ""


def install_esptool():
    """Install esptool.py via pip."""
    print("Installing esptool.py...")
    print()

    python_ok, python_version = check_python()
    if not python_ok:
        print("Error: Python not found", file=sys.stderr)
        return False
    print(f"Python found: {python_version}")

    pip_ok, pip_version = check_pip()
    if not pip_ok:
        print("Error: pip not found", file=sys.stderr)
        print("Install pip first", file=sys.stderr)
        return False
    print(f"Pip found: {pip_version.split()[0]}")

    success, _ = run_command(f"{sys.executable} -m pip install esptool")
    if not success:
        print("Failed to install esptool", file=sys.stderr)
        return False

    success, version_output = run_command("esptool.py version")
    if not success:
        print("esptool.py installation verification failed", file=sys.stderr)
        return False

    print("esptool.py installed successfully!")
    print(version_output.strip())
    print()
    print("Usage: esptool.py --help")
    return True


def main():
    parser = argparse.ArgumentParser(description="Install ESP32 flashing tools")
    parser.parse_args()

    if not install_esptool():
        print("Installation failed!", file=sys.stderr)
        sys.exit(1)

    print("Installation completed successfully!")


if __name__ == "__main__":
    main()
