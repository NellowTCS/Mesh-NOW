#!/usr/bin/env python3
"""
ESP32 Firmware Flasher
Python version - Platform agnostic with TUI
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path

try:
    from rich.console import Console
    from rich.progress import Progress, SpinnerColumn, TextColumn, BarColumn
    from rich.panel import Panel
    from rich.text import Text
    from rich.prompt import Prompt
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False

def setup_console():
    """Setup console for output"""
    if RICH_AVAILABLE:
        return Console()
    else:
        return None

def run_command(cmd, console=None, ci_mode=False, cwd=None):
    """Run a command and return success"""
    try:
        if ci_mode:
            result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
        else:
            result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)

        if result.returncode != 0:
            if console and not ci_mode:
                console.print(f"[red]Command failed: {cmd}[/red]")
                console.print(f"[dim]{result.stderr}[/dim]")
            return False, result.stderr
        return True, result.stdout
    except Exception as e:
        if console and not ci_mode:
            console.print(f"[red]Error running command: {e}[/red]")
        return False, str(e)

def check_firmware_files(console, ci_mode=False):
    """Check if required firmware files exist"""
    required_files = ["mesh-now.bin", "bootloader.bin", "partition-table.bin"]

    missing_files = []
    for file in required_files:
        if not Path(file).exists():
            missing_files.append(file)

    if missing_files:
        if console and not ci_mode:
            console.print("[red]Missing firmware files:[/red]")
            for file in missing_files:
                console.print(f"  - {file}")
        return False

    if console and not ci_mode:
        console.print("[green]✓ All firmware files found[/green]")

    return True

def get_file_size(file_path):
    """Get file size in human readable format"""
    size = Path(file_path).stat().st_size
    for unit in ['B', 'KB', 'MB', 'GB']:
        if size < 1024.0:
            return f"{size:.1f}{unit}"
        size /= 1024.0
    return f"{size:.1f}TB"

def flash_firmware(target, port, console, ci_mode=False):
    """Flash firmware to ESP32"""
    if console and not ci_mode:
        console.print(f"[bold blue]Flashing firmware to {target} on {port}[/bold blue]")
        console.print("[yellow]Make sure your ESP32 is in download mode (hold BOOT while pressing RESET)[/yellow]")
        console.print()

        # Show file sizes
        console.print("[dim]Firmware files:[/dim]")
        for file in ["bootloader.bin", "partition-table.bin", "mesh-now.bin"]:
            if Path(file).exists():
                size = get_file_size(file)
                console.print(f"  {file}: {size}")
        console.print()

    # Check esptool
    success, _ = run_command("esptool.py --version", console, ci_mode)
    if not success:
        if console and not ci_mode:
            console.print("[red]esptool.py not found. Installing...[/red]")
            # Try to install
            install_success = run_command(f"{sys.executable} -m pip install esptool", console, ci_mode)[0]
            if not install_success:
                console.print("[red]Failed to install esptool.py[/red]")
                return False
        else:
            return False

    # Flash command
    flash_cmd = f"""esptool.py --chip {target} --port {port} --baud 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 40m --flash_size detect 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 mesh-now.bin"""

    if console and not ci_mode:
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
            console=console,
        ) as progress:
            task = progress.add_task("Flashing firmware...", total=100)

            # Simulate progress (esptool doesn't provide progress info)
            import time
            for i in range(0, 101, 10):
                progress.update(task, completed=i)
                time.sleep(0.2)

            success, output = run_command(flash_cmd, console, ci_mode)
            progress.update(task, completed=100)
    else:
        success, output = run_command(flash_cmd, console, ci_mode)

    if success:
        if console and not ci_mode:
            console.print("[green]✓ Flash complete![/green]")
            console.print()
            console.print("[bold]Next steps:[/bold]")
            console.print("  1. Reset your ESP32 (press RESET button)")
            console.print("  2. Connect to 'MESH-NOW' WiFi network")
            console.print("  3. Password: 'password'")
            console.print("  4. Open http://192.168.4.1 in your browser")
        elif ci_mode:
            print("Firmware flashed successfully")
        return True
    else:
        if console and not ci_mode:
            console.print("[red]Flash failed![/red]")
        return False

def main():
    parser = argparse.ArgumentParser(description="Flash ESP32 firmware")
    parser.add_argument("target", nargs="?", default="esp32",
                       help="ESP32 target (esp32, esp32s2, esp32s3, esp32c3, esp32c6)")
    parser.add_argument("port", nargs="?", default="/dev/ttyUSB0" if os.name != 'nt' else "COM1",
                       help="Serial port")
    parser.add_argument("--ci", action="store_true", help="CI mode - minimal output")
    args = parser.parse_args()

    console = setup_console() if not args.ci else None

    if console and not args.ci:
        # Show header
        title = Text(f"ESP32 Firmware Flasher - {args.target}", style="bold blue")
        panel = Panel(title, border_style="blue")
        console.print(panel)
        console.print()

    # Check firmware files
    if not check_firmware_files(console, args.ci):
        if console and not args.ci:
            console.print("[red]Cannot proceed without firmware files[/red]")
        sys.exit(1)

    # Flash firmware
    success = flash_firmware(args.target, args.port, console, args.ci)

    if not success:
        sys.exit(1)

if __name__ == "__main__":
    main()