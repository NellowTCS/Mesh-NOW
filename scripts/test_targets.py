#!/usr/bin/env python3
"""
Quick test script to verify all targets configure successfully
"""

import os
import sys
import subprocess
import shutil
from pathlib import Path

try:
    from rich.console import Console
    from rich.table import Table
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False

def check_idf_setup(console):
    """Check if ESP-IDF environment is set up"""
    idf_path = os.environ.get('IDF_PATH')
    if not idf_path:
        console.print("[red]Error: ESP-IDF environment not set up[/red]")
        console.print("Please run: source $ESP_IDF_PATH/export.sh")
        sys.exit(1)
    return idf_path

def run_command(cmd, cwd=None, console=None, silent=True):
    """Run a command silently and return success"""
    try:
        result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=silent, text=True)
        return result.returncode == 0
    except Exception:
        return False

def test_target(target, console, script_dir):
    """Test configuration for a target"""
    build_dir = script_dir.parent / "build"

    # Clean build dir
    if build_dir.exists():
        shutil.rmtree(build_dir)

    # Set target
    if not run_command(f"idf.py set-target {target}", console=console):
        return "Target setup failed"

    # Copy config if exists
    config_file = script_dir.parent / "configs" / f"sdkconfig.{target}"
    if config_file.exists():
        run_command(f"cp {config_file} sdkconfig.defaults", console=console)

    # Test configuration
    if run_command("idf.py reconfigure", console=console):
        return "Configuration OK"
    else:
        return "Configuration failed"

def main():
    # Setup console
    if RICH_AVAILABLE:
        console = Console()
    else:
        class PlainConsole:
            def print(self, *args, **kwargs):
                if args:
                    import re
                    text = re.sub(r'\[.*?\]', '', str(args[0]))
                    print(text)
                else:
                    print()
        console = PlainConsole()

    # Check IDF setup
    check_idf_setup(console)

    script_dir = Path(__file__).parent
    os.chdir(script_dir.parent)  # Change to project root

    targets = ["esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"]

    console.print("Testing all ESP32 targets for Mesh-NOW")
    console.print("=" * 40)
    console.print()

    results = []
    for target in targets:
        console.print(f"Testing {target}...")
        result = test_target(target, console, script_dir)
        results.append(f"{target}: {result}")

    console.print()
    console.print("Results:")

    if RICH_AVAILABLE:
        table = Table()
        table.add_column("Target", style="cyan")
        table.add_column("Status", style="green")
        for result in results:
            target, status = result.split(": ", 1)
            table.add_row(target, status)
        console.print(table)
    else:
        for result in results:
            console.print(f"   {result}")

if __name__ == "__main__":
    main()